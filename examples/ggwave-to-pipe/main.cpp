#include "ggwave/ggwave.h"
#include "ggwave-common.h"
#include "ggwave-common-sdl2.h"
#include <SDL.h>
#include <cstdio>
#include <thread>
#include <stdbool.h>

// return true if the file specified by the filename exists
bool file_exists(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    bool is_exist = false;
    if (fp != NULL)
    {
        is_exist = true;
        fclose(fp);
    }
    return is_exist;
}

int main(int argc, char** argv) {
    printf("Usage: %s [-cN]\n", argv[0]);
    printf("    -cN - select capture device N\n");
    printf("\n");

    auto argm = parseCmdArguments(argc, argv);
    int captureId = argm["c"].empty() ? 0 : std::stoi(argm["c"]);

    if (GGWave_init(0, captureId) == false) {
        fprintf(stderr, "Failed to initialize GGWave\n");
        return -1;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
		
		if (!file_exists("/tmp/inhibit"))
			GGWave_mainLoop();
		}

    GGWave_deinit();

    SDL_CloseAudio();
    SDL_Quit();

    return 0;
}
