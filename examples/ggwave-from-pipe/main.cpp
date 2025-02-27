/* 
 * 
 * Work In Progress: ggwave-to-file -> ggwave-from-pipe 
 * 
 * Target: We should read pipe and play that out
 *
 * WIP:
 * 
 * 	./ggwave-from-pipe > /tmp/t.wav
 * 	
 * https://gist.github.com/armornick/3447121
 * 
 */

#include "ggwave/ggwave.h"

#include "ggwave-common.h"
#include "ggwave-common-sdl2.h"

#include <SDL.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "ggwave-common.h"
#include <cstdio>
#include <cstring>
#include <iostream>

#include <stdio.h>
#include <errno.h>

// fifo
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#define FIFO_NAME "/tmp/fifo_in"

void my_audio_callback(void *userdata, Uint8 *stream, int len);
static Uint8 *audio_pos;
static Uint32 audio_len;

int main(int argc, char** argv) {
	
	struct stat st;

    // Check if FIFO already exists and is a named pipe
    if (stat(FIFO_NAME, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            std::cerr << "Error: " << FIFO_NAME << " exists but is not a FIFO!\n";
            return 1;
        }
    } else {
        // FIFO does not exist, create it
        if (mkfifo(FIFO_NAME, 0666) == -1) {
            std::cerr << "Error creating FIFO!\n";
            return 1;
        }
    }
    



    #if defined(_WIN32)
    const std::string & defaultFile = "audio.wav";
    #else
    const std::string & defaultFile = "/dev/stdout";
    #endif

    fprintf(stderr, "Usage: %s [-vN] [-sN] [-pN] [-lN] [-d]\n", argv[0]);
    fprintf(stderr, "    -fF - output filename, (default: %s)\n", defaultFile.c_str());
    fprintf(stderr, "    -vN - output volume, N in (0, 100], (default: 50)\n");
    fprintf(stderr, "    -sN - output sample rate, N in [%d, %d], (default: %d)\n", (int) GGWave::kSampleRateMin, (int) GGWave::kSampleRateMax, (int) GGWave::kDefaultSampleRate);
    fprintf(stderr, "    -pN - select the transmission protocol id (default: 1)\n");
    fprintf(stderr, "    -lN - fixed payload length of size N, N in [1, %d]\n", GGWave::kMaxLengthFixed);
    fprintf(stderr, "    -d  - use Direct Sequence Spread (DSS)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    Available protocols:\n");

    const auto & protocols = GGWave::Protocols::kDefault();
    for (int i = 0; i < (int) protocols.size(); ++i) {
        const auto & protocol = protocols[i];
        if (protocol.enabled == false) {
            continue;
        }
        fprintf(stderr, "      %d - %s\n", i, protocol.name);
    }
    fprintf(stderr, "\n");

    if (argc < 1) {
        return -1;
    }

    const auto argm = parseCmdArguments(argc, argv);

    if (argm.count("h") > 0) {
        return 0;
    }

    const int   volume        = argm.count("v") == 0 ? 50 : std::stoi(argm.at("v"));
    const std::string & file  = argm.count("f") == 0 ? defaultFile : argm.at("f");
    const float sampleRateOut = argm.count("s") == 0 ? GGWave::kDefaultSampleRate : std::stof(argm.at("s"));
    const int   protocolId    = argm.count("p") == 0 ?  1 : std::stoi(argm.at("p"));
    const int   payloadLength = argm.count("l") == 0 ? -1 : std::stoi(argm.at("l"));
    const bool  useDSS        = argm.count("d") >  0;

    if (volume <= 0 || volume > 100) {
        fprintf(stderr, "Invalid volume\n");
        return -1;
    }

    if (sampleRateOut < GGWave::kSampleRateMin || sampleRateOut > GGWave::kSampleRateMax) {
        fprintf(stderr, "Invalid sample rate: %g\n", sampleRateOut);
        return -1;
    }

    if (protocolId < 0 || protocolId >= (int) protocols.size()) {
        fprintf(stderr, "Invalid transmission protocol id\n");
        return -1;
    }

    if (protocols[protocolId].enabled == false) {
        fprintf(stderr, "Protocol %d is not enabled\n", protocolId);
        return -1;
    }

    // fprintf(stderr, "Enter a text message:\n");
	// Get payload from input
    // std::string message;
    // std::getline(std::cin, message);

while (1)
{

	// Get payload from FIFO
	std::string message;
	char buffer[100];
	
	fprintf(stderr, "waiting for fifo input ... (/tmp/fifo_in) \n");
		
	// Open FIFO for reading
    int fd = open(FIFO_NAME, O_RDONLY);
    if (fd == -1) {
        std::cerr << "Error opening FIFO for reading\n";
        return 1;
    }

    // Read from FIFO
    int bytesRead = read(fd, buffer, sizeof(buffer) - 1);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0'; // Null-terminate the string
        std::cout << "Received: " << buffer << std::endl;
    }

    // Close FIFO
    close(fd);
    
    // Copy buffer to message
    message = std::string(buffer, bytesRead);


    if (message.size() == 0) {
        fprintf(stderr, "Invalid message: size = 0\n");
        return -2;
    }

    if (message.size() > 140) {
        fprintf(stderr, "Invalid message: size > 140\n");
        return -3;
    }

    fprintf(stderr, "Generating waveform for message '%s' ...\n", message.c_str());

    GGWave::OperatingMode mode = GGWAVE_OPERATING_MODE_RX_AND_TX;
    if (useDSS) mode |= GGWAVE_OPERATING_MODE_USE_DSS;

    GGWave ggWave({
        payloadLength,
        GGWave::kDefaultSampleRate,
        sampleRateOut,
        GGWave::kDefaultSampleRate,
        GGWave::kDefaultSamplesPerFrame,
        GGWave::kDefaultSoundMarkerThreshold,
        GGWAVE_SAMPLE_FORMAT_F32,
        GGWAVE_SAMPLE_FORMAT_I16,
        mode,
    });
    ggWave.init(message.size(), message.data(), GGWave::TxProtocolId(protocolId), volume);

    const auto nBytes = ggWave.encode();
    if (nBytes == 0) {
        fprintf(stderr, "Failed to generate waveform!\n");
        return -4;
    }

    std::vector<char> bufferPCM(nBytes);
    std::memcpy(bufferPCM.data(), ggWave.txWaveform(), nBytes);

    fprintf(stderr, "Output file = %s\n", file.c_str());
    fprintf(stderr, "Output size = %d bytes\n", (int) bufferPCM.size());

    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 1;
    format.sampleRate = sampleRateOut;
    format.bitsPerSample = 16;

	// Write WAV
    fprintf(stderr, "Writing WAV data ...\n");

    drwav wav;
    drwav_init_file_write(&wav, file.c_str(), &format, NULL);
    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, bufferPCM.size()/2, bufferPCM.data());

    fprintf(stderr, "WAV frames written = %d\n", (int) framesWritten);
    drwav_uninit(&wav);
    
    
    // TODO: Inhibit ggwave-to-pipe (decoding) running on same host
    
		int fd_inhibit = open("/tmp/inhibit", O_WRONLY | O_CREAT | O_EXCL, 0644);
		// If an error occured, print out more information
		if (fd_inhibit == -1) {
			printf("There was a problem creating /tmp/inhibit \n");
			if (errno == EEXIST) {
			  printf("The file already exists.\n");
			} else {
			  printf("Unknown errno: %d\n", errno);
			}

		}
		// Close the file now that we are done with it
		close(fd_inhibit);
    
    
    // Play wav
    // https://gigi.nullneuron.net/gigilabs/playing-a-wav-file-using-sdl2/
    
    fprintf(stderr, "Playing wav file \n");
    
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
            return (1);
	}
	SDL_AudioSpec wavSpec;
	Uint32 wavLength;
	Uint8 *wavBuffer;
	// TODO: remove hardcoded wav filename
	SDL_LoadWAV("/tmp/t.wav", &wavSpec, &wavBuffer, &wavLength);
	
	// set the callback function
	wavSpec.callback = my_audio_callback;
	wavSpec.userdata = NULL;
	audio_pos = wavBuffer; // copy sound buffer
	audio_len = wavLength; // copy file length
	
    SDL_AudioDeviceID deviceId = SDL_OpenAudioDevice(NULL, 0, &wavSpec, NULL, 0);    
	int success = SDL_QueueAudio(deviceId, wavBuffer, wavLength);
	SDL_PauseAudioDevice(deviceId, 0);
	while ( audio_len > 0 ) {
		SDL_Delay(100); 
	}
	SDL_CloseAudioDevice(deviceId);
	SDL_FreeWAV(wavBuffer);
	SDL_Quit();

	// TODO: Allow ggwave-to-pipe (decoding) running on same host
	

    // Attempt to delete the file
    if (! remove("/tmp/inhibit") == 0) {
        printf("Error: Unable to delete inhibit file.\n");
    }

}

    return 0;
}

void my_audio_callback(void *userdata, Uint8 *stream, int len) 
{
	
	if (audio_len ==0)
		return;
	len = ( (unsigned int) len > audio_len ? audio_len : len );
	SDL_memcpy (stream, audio_pos, len);	
	audio_pos += len;
	audio_len -= len;
}

