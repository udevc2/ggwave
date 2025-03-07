#include "ggwave/ggwave.h"

#include "ggwave-common.h"
#include "ggwave-common-sdl2.h"
#include <SDL.h>
#include <cstdio>
#include <string>
#include <mutex>
#include <thread>
#include <iostream>

// fifo
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#define FIFO_NAME_OUTPUT "/tmp/fifo_out"
#define FIFO_NAME_INPUT "/tmp/fifo_in"


int main(int argc, char** argv) {
    printf("Usage: %s [-cN] [-pN] [-tN] [-lN]\n", argv[0]);
    printf("    -cN - select capture device N\n");
    printf("    -pN - select playback device N\n");
    printf("    -tN - transmission protocol\n");
    printf("    -lN - fixed payload length of size N, N in [1, %d]\n", GGWave::kMaxLengthFixed);
    printf("    -d  - use Direct Sequence Spread (DSS)\n");
    printf("    -v  - print generated tones on resend\n");
    printf("\n");

    const auto argm          = parseCmdArguments(argc, argv);
    const int  captureId     = argm.count("c") == 0 ?  0 : std::stoi(argm.at("c"));
    const int  playbackId    = argm.count("p") == 0 ?  0 : std::stoi(argm.at("p"));
    const int  txProtocolId  = argm.count("t") == 0 ?  1 : std::stoi(argm.at("t"));
    const int  payloadLength = argm.count("l") == 0 ? -1 : std::stoi(argm.at("l"));
    const bool useDSS        = argm.count("d") >  0;
    const bool printTones    = argm.count("v") >  0;


    // Prepare fifo
    struct stat st;
    if (stat(FIFO_NAME_INPUT, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            std::cerr << "Error: " << FIFO_NAME_INPUT << " exists but is not a FIFO!\n";
            return 1;
        }
    } else {
        // FIFO does not exist, create it
        if (mkfifo(FIFO_NAME_INPUT, 0666) == -1) {
            std::cerr << "Error creating FIFO!\n";
            return 1;
        }
    }

    if (GGWave_init(playbackId, captureId, payloadLength, 0.0f, useDSS) == false) {
        fprintf(stderr, "Failed to initialize GGWave\n");
        return -1;
    }

    auto ggWave = GGWave_instance();
    printf("Available Tx protocols:\n");
    const auto & protocols = GGWave::Protocols::kDefault();
    for (int i = 0; i < (int) protocols.size(); ++i) {
        const auto & protocol = protocols[i];
        if (protocol.enabled == false) {
            continue;
        }
        printf("      %d - %s\n", i, protocol.name);
    }

    if (txProtocolId < 0) {
        fprintf(stderr, "Unknown Tx protocol %d\n", txProtocolId);
        return -3;
    }

    printf("Selecting Tx protocol %d\n", txProtocolId);

    std::mutex mutex;
    std::thread inputThread([&]() {
        std::string inputOld = "";
        
        //
        // Output to audio from fifo (/tmp/fifo_in)
        //
        while (true) {
            // Get payload from FIFO
            std::string message;
            printf("Waiting for fifo input ( %s ) \n", FIFO_NAME_INPUT);
            
            // Open FIFO for reading
            int fd = open(FIFO_NAME_INPUT, O_RDONLY);
            if (fd == -1) {
                std::cerr << "Error opening FIFO for reading\n";
                return 1;
            }

            // Read from FIFO in binary-safe manner
            // 180 is ggwave limit
            #define BUFFER_SIZE 180
            unsigned char buffer[BUFFER_SIZE];  // Use unsigned char for binary data
            int bytesRead = read(fd, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                std::cout << "Received " << bytesRead << " bytes\n";

                // Process binary data (example: print raw hex values)
                for (int i = 0; i < bytesRead; ++i) {
                    printf("%02X ", buffer[i]);  // Print in hexadecimal
                }
                printf("\n");
            } else if (bytesRead == 0) {
                std::cout << "FIFO closed by writer\n";
            } else {
                std::cerr << "Error reading from FIFO\n";
            }
            close(fd);
            message = std::string(reinterpret_cast<const char*>(buffer), bytesRead);
            // Output the audio
            {
                std::lock_guard<std::mutex> lock(mutex);
                ggWave->init(message.size(), message.data(), GGWave::TxProtocolId(txProtocolId), 10);
            }
            
            
            // default send
            if ( 0 )
            {
                std::string input;
                printf("Enter text: ");
                fflush(stdout);
                getline(std::cin, input);
                if (input.empty()) {
                    printf("Re-sending ...\n");
                    input = inputOld;

                    if (printTones) {
                        printf("Printing generated waveform tones (Hz):\n");
                        const auto & protocol = protocols[txProtocolId];
                        const auto tones = ggWave->txTones();
                        for (int i = 0; i < (int) tones.size(); ++i) {
                            if (tones[i] < 0) {
                                printf(" - end tx\n");
                                continue;
                            }
                            const auto freq_hz = (protocol.freqStart + tones[i])*ggWave->hzPerSample();
                            printf(" - tone %3d: %f\n", i, freq_hz);
                        }
                    }
                } else {
                    printf("Sending ...\n");
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ggWave->init(input.size(), input.data(), GGWave::TxProtocolId(txProtocolId), 10);
                }
                inputOld = input;
            }
            
            
        }
    });

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            std::lock_guard<std::mutex> lock(mutex);
            
            GGWave_mainLoop();            
            
            /*  
             * Add to ggwave.h:
             * 
             *      bool hasNewRxData() const { return m_rx.hasNewRxData; }
             *      void resetNewRxFlag() { m_rx.hasNewRxData = false; }
             * 
             */
            if (ggWave->hasNewRxData()) {
                
                auto receivedData = ggWave->rxData();
                std::cout << "New data received: ";
                for (auto byte : receivedData) {
                    std::cout << static_cast<char>(byte);
                }
                std::cout << std::endl;
                
                //
                // WIP: Write fifo here
                // 
                
                    struct stat st;
                    // Check if FIFO already exists and is a named pipe
                    if (stat(FIFO_NAME_OUTPUT, &st) == 0) {
                        if (!S_ISFIFO(st.st_mode)) {
                            printf(" %s exists but is not a FIFO! \n",FIFO_NAME_OUTPUT);
                        }
                    } else {
                        // FIFO does not exist, create it
                        if (mkfifo(FIFO_NAME_OUTPUT, 0666) == -1) {
                            printf(" error creating fifo \n");
                        }
                    }
                    
                    // If no process is reading from the FIFO, open(FIFO_NAME_OUTPUT, O_WRONLY); blocks indefinitely.
                    // int fd = open(FIFO_NAME_OUTPUT, O_WRONLY | O_NONBLOCK);
                    
                    // Open FIFO for writing
                    int fd = open(FIFO_NAME_OUTPUT, O_WRONLY);
                    if (fd == -1) {
                        printf("Error opening FIFO for writing \n");
                    }
                    printf(" data: %s len: %i (mod) \n", receivedData.data(), ggWave->getRxDataLength() );
                    write(fd, receivedData.data(), ggWave->getRxDataLength() );
                    close(fd);
                
                //
                // end of wip
                //
                
                ggWave->resetNewRxFlag();
            }
            
           
            
        }
    }

    inputThread.join();

    GGWave_deinit();

    SDL_CloseAudio();
    SDL_Quit();

    return 0;
}
