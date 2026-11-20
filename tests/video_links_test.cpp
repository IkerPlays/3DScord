// Host test of Utils::VideoLinks::youtubeId. Build and run: see .github/workflows/tests.yml
#include "utils/video_links.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                                                                                                  \
	do {                                                                                                                                             \
		if (!(cond)) {                                                                                                                               \
			printf("FAILED line %d: %s\n", __LINE__, #cond);                                                                                          \
			failures++;                                                                                                                              \
		}                                                                                                                                            \
	} while (0)

int main() {
	using Utils::VideoLinks::youtubeId;
	CHECK(youtubeId("https://www.youtube.com/watch?v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=10") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://youtu.be/dQw4w9WgXcQ?t=5") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://m.youtube.com/shorts/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://www.youtube.com/embed/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://music.youtube.com/watch?v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://www.youtube.com/live/dQw4w9WgXcQ?si=abc") == "dQw4w9WgXcQ");
	CHECK(youtubeId("https://example.com/watch?v=dQw4w9WgXcQ").empty());
	CHECK(youtubeId("https://www.youtube.com/channel/UC123").empty());
	CHECK(youtubeId("https://www.youtube.com/watch?v=short").empty());
	CHECK(youtubeId("https://evilyoutube.com/watch?v=dQw4w9WgXcQ").empty());
	CHECK(youtubeId("").empty());
	printf(failures ? "%d failures\n" : "all good\n", failures);
	return failures ? 1 : 0;
}
