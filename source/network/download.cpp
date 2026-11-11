#include "network/download.h"
#include "network/http_client.h"
#include <cstdio>
#include <curl/curl.h>
#include <sys/stat.h>

namespace Network {

namespace {

struct Context {
	FILE *file;
	const DownloadControl *control;
	bool writeFailed;
};

size_t writeChunk(void *data, size_t size, size_t nmemb, void *userp) {
	auto *ctx = static_cast<Context *>(userp);
	const size_t written = fwrite(data, size, nmemb, ctx->file);
	if (written != nmemb) {
		ctx->writeFailed = true;
	}
	return written * size;
}

int reportProgress(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
	auto *ctx = static_cast<Context *>(userp);
	if (ctx->control->downloaded) {
		ctx->control->downloaded->store(dlnow);
	}
	if (ctx->control->total && dltotal > 0) {
		ctx->control->total->store(dltotal);
	}
	return ctx->control->cancel && ctx->control->cancel->load() ? 1 : 0;
}

} // namespace

bool downloadToFile(const std::string &url, const std::string &dest, const DownloadControl &control, std::string &error,
                    long long expectedSize) {
	FILE *file = fopen(dest.c_str(), "wb");
	if (!file) {
		error = "Cannot write to the SD card";
		return false;
	}

	CURL *easy = curl_easy_init();
	if (!easy) {
		fclose(file);
		remove(dest.c_str());
		error = "Network unavailable";
		return false;
	}

	HttpResponse unused;
	HttpRequest request{url, "GET", "", {}, 0};
	curl_slist *headers = prepareTransfer(easy, request, unused);

	Context ctx{file, &control, false};
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeChunk);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx);
	// The default header callback reserves a buffer as big as the file.
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
		if (control.cancel && control.cancel->load()) {
			error = "Cancelled";
		} else if (ctx.writeFailed) {
			error = "SD card full or not writable";
		} else {
			error = std::string("Download failed: ") + curl_easy_strerror(code);
		}
		return false;
	}

	struct stat st;
	if (stat(dest.c_str(), &st) != 0 || (expectedSize > 0 && st.st_size != expectedSize)) {
		remove(dest.c_str());
		error = "Downloaded file is incomplete";
		return false;
	}
	return true;
}

} // namespace Network
