#include "utils/transfers.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "network/download.h"
#include "network/http_client.h"
#include "network/network_manager.h"
#include "utils/system_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sys/stat.h>

namespace Utils {

namespace {

constexpr long long MAX_UPLOAD_BYTES = 4LL * 1024 * 1024;
constexpr long long MAX_DOWNLOAD_BYTES = 64LL * 1024 * 1024;

std::string sanitize(const std::string &name) {
	std::string out;
	for (char c : name) {
		out += (std::string("\\/:*?\"<>|").find(c) != std::string::npos || (unsigned char)c < 32) ? '_' : c;
	}
	while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
		out.pop_back();
	}
	if (out.empty()) {
		out = "file";
	}
	if (out.size() > 80) {
		out.resize(80);
	}
	return out;
}

bool exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

std::string apiError(const Network::HttpResponse &resp) {
	rapidjson::Document doc;
	doc.Parse(resp.body.c_str());
	if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("message") && doc["message"].IsString()) {
		return doc["message"].GetString();
	}
	return resp.error.empty() ? "HTTP " + std::to_string(resp.statusCode) : resp.error;
}

std::string randomBoundary() {
	static const char hex[] = "0123456789abcdef";
	std::string b = "----3DScord";
	for (int i = 0; i < 16; i++) {
		b += hex[rand() % 16];
	}
	return b;
}

} // namespace

Transfers &Transfers::getInstance() {
	static Transfers instance;
	return instance;
}

std::string Transfers::downloadDir() { return std::string(CONFIG_DIR_PATH) + "/downloads"; }

void Transfers::enqueue(std::function<void()> job) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (stopping) {
			return;
		}
		jobs.push_back(std::move(job));
		if (!started) {
			started = true;
			worker.start([this] { run(); }, 2, 64 * 1024);
		}
	}
	cv.notify_one();
}

void Transfers::run() {
	for (;;) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [this] { return stopping || !jobs.empty(); });
			if (stopping) {
				return;
			}
			job = std::move(jobs.front());
			jobs.pop_front();
		}
		job();
	}
}

void Transfers::shutdown() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	cancel = true;
	cv.notify_all();
	if (worker.joinable()) {
		worker.join();
	}
}

void Transfers::report(const std::string &text) {
	std::lock_guard<std::mutex> lock(mutex);
	message = text;
}

std::string Transfers::takeMessage() {
	std::lock_guard<std::mutex> lock(mutex);
	std::string out;
	out.swap(message);
	return out;
}

std::string Transfers::uniquePath(const std::string &dir, const std::string &filename) {
	const std::string clean = sanitize(filename);
	const size_t dot = clean.rfind('.');
	const std::string stem = dot == std::string::npos || dot == 0 ? clean : clean.substr(0, dot);
	const std::string ext = dot == std::string::npos || dot == 0 ? "" : clean.substr(dot);
	std::string path = dir + "/" + clean;
	for (int i = 1; exists(path) && i < 1000; i++) {
		path = dir + "/" + stem + " (" + std::to_string(i) + ")" + ext;
	}
	return path;
}

void Transfers::saveAttachment(const std::string &url, const std::string &filename, long long size) {
	if (size > MAX_DOWNLOAD_BYTES) {
		report(TR("transfer.too_big"));
		return;
	}
	report(TR("transfer.saving"));
	enqueue([this, url, filename, size] {
		mkdir("sdmc:/3ds", 0777);
		mkdir(CONFIG_DIR_PATH, 0777);
		const std::string dir = downloadDir();
		mkdir(dir.c_str(), 0777);

		const std::string path = uniquePath(dir, filename);
		Network::DownloadControl control;
		control.cancel = &cancel;
		std::string error;
		if (Network::downloadToFile(url, path, control, error, 0)) {
			Logger::log("[Transfers] Saved %s", path.c_str());
			report(TR("transfer.saved") + " " + path.substr(path.rfind('/') + 1));
		} else {
			report(error);
		}
	});
}

void Transfers::uploadFile(const std::string &channelId, const std::string &path, const std::string &caption) {
	report(TR("transfer.uploading"));
	enqueue([this, channelId, path, caption] {
		struct stat st;
		if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
			report(TR("transfer.read_failed"));
			return;
		}
		if (st.st_size > MAX_UPLOAD_BYTES) {
			report(TR("transfer.too_big"));
			return;
		}
		// The file, the form around it and the copy the request keeps must all fit.
		if ((size_t)st.st_size * 3 + 512 * 1024 > System::heapFreeBytes()) {
			report(TR("transfer.no_memory"));
			return;
		}

		FILE *f = fopen(path.c_str(), "rb");
		if (!f) {
			report(TR("transfer.read_failed"));
			return;
		}
		std::string data((size_t)st.st_size, '\0');
		const size_t got = fread(&data[0], 1, data.size(), f);
		fclose(f);
		if (got != data.size()) {
			report(TR("transfer.read_failed"));
			return;
		}

		std::string name = path.substr(path.rfind('/') + 1);
		for (char &c : name) {
			if (c == '"' || c == '\r' || c == '\n') {
				c = '_';
			}
		}

		rapidjson::StringBuffer s;
		rapidjson::Writer<rapidjson::StringBuffer> w(s);
		w.StartObject();
		w.Key("content");
		w.String(caption.c_str());
		w.Key("flags");
		w.Int(0);
		w.Key("attachments");
		w.StartArray();
		w.StartObject();
		w.Key("id");
		w.Int(0);
		w.Key("filename");
		w.String(name.c_str());
		w.EndObject();
		w.EndArray();
		w.EndObject();

		const std::string boundary = randomBoundary();
		std::string body;
		body.reserve(data.size() + 1024);
		body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n";
		body += "Content-Type: application/json\r\n\r\n";
		body += s.GetString();
		body += "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"files[0]\"; filename=\"" + name + "\"\r\n";
		body += "Content-Type: application/octet-stream\r\n\r\n";
		body += data;
		body += "\r\n--" + boundary + "--\r\n";
		std::string().swap(data);

		const std::string token = Config::getInstance().getToken();
		Network::HttpRequest request{"https://discord.com/api/v10/channels/" + channelId + "/messages", "POST",
		                             std::move(body),
		                             {{"Authorization", token}, {"Content-Type", "multipart/form-data; boundary=" + boundary}},
		                             180};
		Network::HttpResponse resp = Network::NetworkManager::getInstance().perform(std::move(request));
		if (resp.success) {
			report(TR("transfer.uploaded"));
		} else {
			Logger::log("[Transfers] Upload failed: %ld", resp.statusCode);
			report(apiError(resp));
		}
	});
}

void Transfers::sendVoiceMessage(const std::string &channelId, std::string oggData, double seconds,
                                 std::string waveform) {
	report(TR("transfer.uploading"));
	enqueue([this, channelId, data = std::move(oggData), seconds, waveform = std::move(waveform)]() mutable {
		const std::string token = Config::getInstance().getToken();
		const std::string base = "https://discord.com/api/v10/channels/" + channelId;
		Network::HttpClient http;
		http.setAuthToken(token);
		http.setTimeout(30);

		// 1. Ask Discord where the file goes.
		rapidjson::StringBuffer ask;
		rapidjson::Writer<rapidjson::StringBuffer> aw(ask);
		aw.StartObject();
		aw.Key("files");
		aw.StartArray();
		aw.StartObject();
		aw.Key("filename");
		aw.String("voice-message.ogg");
		aw.Key("file_size");
		aw.Uint64(data.size());
		aw.Key("id");
		aw.String("0");
		aw.EndObject();
		aw.EndArray();
		aw.EndObject();
		Network::HttpResponse slot = http.post(base + "/attachments", ask.GetString());
		if (!slot.success) {
			report(apiError(slot));
			return;
		}
		rapidjson::Document doc;
		doc.Parse(slot.body.c_str());
		std::string uploadUrl;
		std::string uploadName;
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("attachments") && doc["attachments"].IsArray() &&
		    doc["attachments"].Size() > 0) {
			const auto &a = doc["attachments"][0];
			if (a.HasMember("upload_url") && a["upload_url"].IsString()) {
				uploadUrl = a["upload_url"].GetString();
			}
			if (a.HasMember("upload_filename") && a["upload_filename"].IsString()) {
				uploadName = a["upload_filename"].GetString();
			}
		}
		if (uploadUrl.empty() || uploadName.empty()) {
			report(TR("voice_msg.failed"));
			return;
		}

		// 2. Put the file there. This is another server: it must not get the account token.
		Network::HttpClient storage;
		storage.setTimeout(120);
		Network::HttpResponse put = storage.put(uploadUrl, data, {{"Content-Type", "audio/ogg"}});
		if (!put.success) {
			Logger::log("[Transfers] Voice upload failed: %ld", put.statusCode);
			report(TR("voice_msg.failed"));
			return;
		}

		// 3. Send the message that points to the uploaded file (flag 8192 = voice message).
		rapidjson::StringBuffer msg;
		rapidjson::Writer<rapidjson::StringBuffer> mw(msg);
		mw.StartObject();
		mw.Key("content");
		mw.String("");
		mw.Key("flags");
		mw.Int(8192);
		mw.Key("attachments");
		mw.StartArray();
		mw.StartObject();
		mw.Key("id");
		mw.String("0");
		mw.Key("filename");
		mw.String("voice-message.ogg");
		mw.Key("uploaded_filename");
		mw.String(uploadName.c_str());
		mw.Key("duration_secs");
		mw.Double(seconds);
		mw.Key("waveform");
		mw.String(waveform.c_str());
		mw.EndObject();
		mw.EndArray();
		mw.EndObject();
		Network::HttpResponse sent = http.post(base + "/messages", msg.GetString());
		report(sent.success ? TR("voice_msg.sent") : apiError(sent));
	});
}

void Transfers::sendSticker(const std::string &channelId, const std::string &stickerId) {
	enqueue([this, channelId, stickerId] {
		rapidjson::StringBuffer s;
		rapidjson::Writer<rapidjson::StringBuffer> w(s);
		w.StartObject();
		w.Key("sticker_ids");
		w.StartArray();
		w.String(stickerId.c_str());
		w.EndArray();
		w.EndObject();

		Network::HttpClient http;
		http.setAuthToken(Config::getInstance().getToken());
		http.setTimeout(30);
		Network::HttpResponse resp =
		    http.post("https://discord.com/api/v10/channels/" + channelId + "/messages", s.GetString());
		report(resp.success ? TR("transfer.sticker_sent") : apiError(resp));
	});
}

} // namespace Utils
