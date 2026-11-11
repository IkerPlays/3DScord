#include "utils/updater.h"
#include "core/config.h"
#include "log.h"
#include "network/http_client.h"
#include <3ds.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <sys/stat.h>
#include <vector>

namespace Utils {

namespace {

constexpr const char *RELEASES_URL = "https://api.github.com/repos/" UPDATE_REPO "/releases?per_page=20";
constexpr const char *TEMP_FILE = CONFIG_DIR_PATH "/update.tmp";
constexpr const char *CIA_ASSET = "3DScord.cia";
constexpr const char *DSX_ASSET = "3DScord.3dsx";
constexpr const char *DEFAULT_3DSX_PATH = "sdmc:/3ds/3DScord/3DScord.3dsx";

struct DownloadContext {
	FILE *file;
	std::atomic<long long> *downloaded;
	std::atomic<long long> *total;
	std::atomic<bool> *cancel;
	bool writeFailed;
};

size_t writeChunk(void *data, size_t size, size_t nmemb, void *userp) {
	auto *ctx = static_cast<DownloadContext *>(userp);
	const size_t written = fwrite(data, size, nmemb, ctx->file);
	if (written != nmemb) {
		ctx->writeFailed = true;
	}
	return written * size;
}

int reportProgress(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
	auto *ctx = static_cast<DownloadContext *>(userp);
	ctx->downloaded->store(dlnow);
	if (dltotal > 0) {
		ctx->total->store(dltotal);
	}
	return ctx->cancel->load() ? 1 : 0;
}

long long versionKey(const Updater::Version &v) {
	// A stable build is newer than any devbuild of the same version.
	const long long dev = v.dev == 0 ? 999999 : v.dev;
	return (((long long)v.major * 1000 + v.minor) * 1000 + v.micro) * 1000000LL + dev;
}

bool fileExists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

void ensureConfigDir() {
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR_PATH, 0777);
}

} // namespace

Updater &Updater::getInstance() {
	static Updater instance;
	return instance;
}

Updater::Version Updater::currentVersion() {
	Version v;
	v.major = APP_VERSION_MAJOR;
	v.minor = APP_VERSION_MINOR;
	v.micro = APP_VERSION_MICRO;
	v.dev = APP_DEV_BUILD;
	return v;
}

std::string Updater::versionString(const Version &v) {
	std::string s = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.micro);
	if (v.dev > 0) {
		s += "-dev." + std::to_string(v.dev);
	}
	return s;
}

bool Updater::parseTag(const std::string &tag, Version &out) {
	const char *p = tag.c_str();
	if (*p == 'v' || *p == 'V') {
		p++;
	}
	int a = 0, b = 0, c = 0, n = 0, consumed = 0;
	if (sscanf(p, "%d.%d.%d%n", &a, &b, &c, &consumed) != 3) {
		return false;
	}
	out = Version{a, b, c, 0};
	p += consumed;
	if (*p == '\0') {
		return true;
	}
	if (sscanf(p, "-dev.%d", &n) == 1 && n > 0) {
		out.dev = n;
		return true;
	}
	return false;
}

bool Updater::isNewer(const Version &candidate, const Version &base) { return versionKey(candidate) > versionKey(base); }

void Updater::setLaunchPath(const char *path) {
	if (path && *path) {
		launchPath = path;
		// The config folder was renamed from TriCord to 3DScord when the app moved there.
		const std::string legacy = std::string(LEGACY_CONFIG_DIR_PATH) + "/";
		if (launchPath.rfind(legacy, 0) == 0) {
			launchPath = std::string(CONFIG_DIR_PATH) + "/" + launchPath.substr(legacy.size());
		}
	}
}

bool Updater::isCiaInstall() { return !envIsHomebrew(); }

float Updater::getProgress() const {
	const long long t = total.load();
	if (t <= 0) {
		return 0.0f;
	}
	return std::min(1.0f, (float)((double)downloaded.load() / (double)t));
}

std::string Updater::getStatus() {
	std::lock_guard<std::mutex> lock(mutex);
	return status;
}

std::string Updater::getError() {
	std::lock_guard<std::mutex> lock(mutex);
	return error;
}

Updater::Release Updater::getRelease() {
	std::lock_guard<std::mutex> lock(mutex);
	return release;
}

void Updater::setStatus(const std::string &message) {
	std::lock_guard<std::mutex> lock(mutex);
	status = message;
}

void Updater::fail(const std::string &message) {
	Logger::log("[Updater] %s", message.c_str());
	{
		std::lock_guard<std::mutex> lock(mutex);
		error = message;
	}
	state = State::FAILED;
}

void Updater::join() {
	if (worker.joinable()) {
		worker.join();
	}
}

void Updater::startCheck(bool includeDevBuilds) {
	join();
	cancelRequested = false;
	state = State::CHECKING;
	setStatus("");
	worker.start([this, includeDevBuilds] { runCheck(includeDevBuilds); }, 1, 64 * 1024);
}

void Updater::startInstall() {
	join();
	cancelRequested = false;
	downloaded = 0;
	total = 0;
	state = State::DOWNLOADING;
	worker.start([this] { runInstall(); }, 1, 64 * 1024);
}

void Updater::runCheck(bool includeDevBuilds) {
	Network::HttpClient http;
	http.setTimeout(10);
	Network::HttpResponse resp = http.get(RELEASES_URL, {{"Accept", "application/vnd.github+json"}});
	if (!resp.success) {
		fail("Update check failed (HTTP " + std::to_string(resp.statusCode) + ")");
		return;
	}

	rapidjson::Document doc;
	doc.Parse(resp.body.c_str());
	if (doc.HasParseError() || !doc.IsArray()) {
		fail("Update check returned invalid data");
		return;
	}

	const Version current = currentVersion();
	const char *wanted = isCiaInstall() ? CIA_ASSET : DSX_ASSET;
	Release best;
	bool found = false;

	for (const auto &r : doc.GetArray()) {
		if (!r.IsObject() || !r.HasMember("tag_name") || !r["tag_name"].IsString()) {
			continue;
		}
		if (r.HasMember("draft") && r["draft"].IsBool() && r["draft"].GetBool()) {
			continue;
		}
		Version v;
		const std::string tag = r["tag_name"].GetString();
		if (!parseTag(tag, v)) {
			continue;
		}
		if (v.dev > 0 && !includeDevBuilds) {
			continue;
		}
		if (!isNewer(v, current) || (found && !isNewer(v, best.version))) {
			continue;
		}
		if (!r.HasMember("assets") || !r["assets"].IsArray()) {
			continue;
		}
		for (const auto &a : r["assets"].GetArray()) {
			if (a.IsObject() && a.HasMember("name") && a["name"].IsString() && strcmp(a["name"].GetString(), wanted) == 0 &&
			    a.HasMember("browser_download_url") && a["browser_download_url"].IsString()) {
				best.tag = tag;
				best.version = v;
				best.assetUrl = a["browser_download_url"].GetString();
				best.assetSize = (a.HasMember("size") && a["size"].IsInt64()) ? a["size"].GetInt64() : 0;
				best.notes = (r.HasMember("body") && r["body"].IsString()) ? r["body"].GetString() : "";
				found = true;
				break;
			}
		}
	}

	if (!found) {
		Logger::log("[Updater] Up to date (%s)", versionString(current).c_str());
		state = State::UP_TO_DATE;
		return;
	}

	Logger::log("[Updater] Update available: %s", best.tag.c_str());
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = best;
	}
	state = State::AVAILABLE;
}

bool Updater::download(const Release &rel, const std::string &dest) {
	ensureConfigDir();
	FILE *file = fopen(dest.c_str(), "wb");
	if (!file) {
		fail("Cannot write to the SD card");
		return false;
	}

	CURL *easy = curl_easy_init();
	if (!easy) {
		fclose(file);
		fail("Network unavailable");
		return false;
	}

	Network::HttpResponse unused;
	Network::HttpRequest request{rel.assetUrl, "GET", "", {}, 0};
	curl_slist *headers = Network::prepareTransfer(easy, request, unused);

	DownloadContext ctx{file, &downloaded, &total, &cancelRequested, false};
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeChunk);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, nullptr);
	curl_easy_setopt(easy, CURLOPT_HEADERDATA, nullptr);
	curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, nullptr);
	curl_easy_setopt(easy, CURLOPT_TIMEOUT, 0L);
	curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, reportProgress);
	curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &ctx);
	curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);

	const CURLcode code = curl_easy_perform(easy);
	curl_slist_free_all(headers);
	curl_easy_cleanup(easy);
	fclose(file);

	if (code != CURLE_OK || ctx.writeFailed) {
		remove(dest.c_str());
		fail(cancelRequested ? "Update cancelled"
		                     : ctx.writeFailed ? "SD card full or not writable"
		                                       : std::string("Download failed: ") + curl_easy_strerror(code));
		return false;
	}

	struct stat st;
	if (stat(dest.c_str(), &st) != 0 || (rel.assetSize > 0 && st.st_size != rel.assetSize)) {
		remove(dest.c_str());
		fail("Downloaded file is incomplete");
		return false;
	}
	return true;
}

bool Updater::installCia(const std::string &path) {
	FILE *in = fopen(path.c_str(), "rb");
	if (!in) {
		fail("Cannot read the downloaded update");
		return false;
	}

	Result rc = amInit();
	if (R_FAILED(rc)) {
		fclose(in);
		fail("Cannot access the installer (AM service)");
		return false;
	}

	Handle cia;
	rc = AM_StartCiaInstall(MEDIATYPE_SD, &cia);
	if (R_FAILED(rc)) {
		fclose(in);
		amExit();
		fail("Cannot start the installation");
		return false;
	}

	std::vector<unsigned char> buffer(128 * 1024);
	u64 offset = 0;
	bool ok = true;
	size_t n;
	while ((n = fread(buffer.data(), 1, buffer.size(), in)) > 0) {
		u32 written = 0;
		rc = FSFILE_Write(cia, &written, offset, buffer.data(), (u32)n, 0);
		if (R_FAILED(rc) || written != n) {
			ok = false;
			break;
		}
		offset += n;
	}
	fclose(in);

	if (ok) {
		rc = AM_FinishCiaInstall(cia);
		ok = R_SUCCEEDED(rc);
	} else {
		AM_CancelCIAInstall(cia);
	}
	amExit();

	if (!ok) {
		fail("Installation failed (invalid CIA or no space left)");
		return false;
	}
	return true;
}

bool Updater::installHomebrew(const std::string &path) {
	std::string target = launchPath;
	if (target.empty() || target.find(".3dsx") == std::string::npos) {
		target = DEFAULT_3DSX_PATH;
	}
	if (target.rfind("sdmc:", 0) != 0 && target[0] == '/') {
		target = "sdmc:" + target;
	}

	// The 3dsx header starts with "3DSX"; refuse anything else instead of breaking the installed app.
	FILE *f = fopen(path.c_str(), "rb");
	char magic[4] = {0};
	bool valid = f && fread(magic, 1, 4, f) == 4 && memcmp(magic, "3DSX", 4) == 0;
	if (f) {
		fclose(f);
	}
	if (!valid) {
		fail("Downloaded file is not a valid 3DSX");
		return false;
	}

	const std::string backup = target + ".bak";
	remove(backup.c_str());
	const bool hadOld = fileExists(target.c_str());
	if (hadOld && rename(target.c_str(), backup.c_str()) != 0) {
		fail("Cannot replace the installed app");
		return false;
	}
	if (rename(path.c_str(), target.c_str()) != 0) {
		if (hadOld) {
			rename(backup.c_str(), target.c_str());
		}
		fail("Cannot write the new version");
		return false;
	}
	remove(backup.c_str());
	return true;
}

void Updater::runInstall() {
	const Release rel = getRelease();
	setStatus("Downloading " + rel.tag);
	if (!download(rel, TEMP_FILE)) {
		return;
	}

	state = State::INSTALLING;
	setStatus("Installing " + rel.tag);
	const bool cia = isCiaInstall();
	const bool ok = cia ? installCia(TEMP_FILE) : installHomebrew(TEMP_FILE);
	remove(TEMP_FILE);
	if (!ok) {
		return;
	}

	Logger::log("[Updater] Installed %s", rel.tag.c_str());
	state = cia ? State::RESTART : State::EXIT_MANUAL;
}

} // namespace Utils
