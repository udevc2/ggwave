/*
 * ggwave-pipe
 * 
 * Pipe data to and from fifo files.
 * 
 * - Reads FIFO /tmp/fifo_in for data to be transmitted over audio link.
 * - Writes FIFO /tmp/fifo_out with data received from audio link.
 * - Creates /tmp/inhibit when audio receive is in progress
 * 
 * 
 * Available Tx protocols:
      0 - Normal
      1 - Fast
      2 - Fastest
      3 - [U] Normal
      4 - [U] Fast
      5 - [U] Fastest
      6 - [DT] Normal
      7 - [DT] Fast
      8 - [DT] Fastest
      9 - [MT] Normal
      10 - [MT] Fast
      11 - [MT] Fastest
 */

#include "ggwave/ggwave.h"
#include "ggwave-common.h"
#include "ggwave-common-sdl2.h"
#include <SDL.h>
#include <cstdio>
#include <string>
#include <mutex>
#include <thread>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <atomic>
#include <signal.h> 

#define FIFO_NAME_OUTPUT "/tmp/fifo_out"
#define FIFO_NAME_INPUT "/tmp/fifo_in"



int g_busyBlock = 0;

void signal_handler(int s){
    printf("Caught signal %d\n",s);
    exit(1); 
}

int main(int argc, char** argv) {
    printf("Usage: %s [-cN] [-pN] [-tN] [-lN]\n", argv[0]);
    printf("    -cN - select capture device N\n");
    printf("    -pN - select playback device N\n");
    printf("    -tN - transmission protocol\n");
    printf("    -lN - fixed payload length of size N, N in [1, %d]\n", GGWave::kMaxLengthFixed);
    printf("    -d  - use Direct Sequence Spread (DSS)\n");
    printf("\n");

    const auto argm          = parseCmdArguments(argc, argv);
    const int  captureId     = argm.count("c") == 0 ?  0 : std::stoi(argm.at("c"));
    const int  playbackId    = argm.count("p") == 0 ?  0 : std::stoi(argm.at("p"));
    const int  txProtocolId  = argm.count("t") == 0 ?  1 : std::stoi(argm.at("t"));
    const int  payloadLength = argm.count("l") == 0 ? -1 : std::stoi(argm.at("l"));
    const bool useDSS        = argm.count("d") >  0;

    // Signal handler
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = signal_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);


    // Prepare FIFO_NAME_INPUT
    struct stat st;
    if (stat(FIFO_NAME_INPUT, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            std::cerr << "Error: " << FIFO_NAME_INPUT << " exists but is not a FIFO!\n";
            return 1;
        }
    } else {
        if (mkfifo(FIFO_NAME_INPUT, 0666) == -1) {
            std::cerr << "Error creating FIFO!\n";
            return 1;
        }
    }
    
    // Prepare FIFO_NAME_OUTPUT
    if (stat(FIFO_NAME_OUTPUT, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            printf(" %s exists but is not a FIFO! \n",FIFO_NAME_OUTPUT);
        }
    } else {
        if (mkfifo(FIFO_NAME_OUTPUT, 0666) == -1) {
            printf(" error creating fifo \n");
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
        while ( true ) {
            std::string message;
            printf("Waiting for fifo input ( %s ) \n", FIFO_NAME_INPUT);
            
            // Open FIFO for reading
            int fd = open(FIFO_NAME_INPUT, O_RDONLY);
            if (fd == -1) {
                std::cerr << "Error opening FIFO for reading\n";
                return 1;
            }

            // “Currently, the actual limit for the message length is 183 bytes. 
            // The reason is that we append 40% ECC bytes at the end of the message 
            // and the total length becomes 1.4*183 = 256. At this point, the error 
            // correction library that is used stops working.”
            // https://github.com/ggerganov/ggwave/discussions/34
            
            #define BUFFER_SIZE 180
            unsigned char buffer[BUFFER_SIZE];  // Use unsigned char for binary data
            // Read from FIFO in binary-safe manner
            int bytesRead = read(fd, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                std::cout << "Received " << bytesRead << " bytes\n";
                for (int i = 0; i < bytesRead; ++i) {
                    printf("%02X ", buffer[i]);
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
        }
    });

    while ( true ) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            std::lock_guard<std::mutex> lock(mutex);
            
            // Audio listen
            GGWave_mainLoop();            
            
            if ( ggWave->recevingRxData() && g_busyBlock == 0 )
            {
                std::cout << "Start receiving\n";
                g_busyBlock = 1;
                const char *filename = "/tmp/inhibit";
                FILE *fp_inhibit = fopen(filename, "w");
                if (fp_inhibit == NULL)
                {
                    printf("Error opening the file %s", filename);
                    return -1;
                }
                fprintf(fp_inhibit, "rx-active\n");
                fclose(fp_inhibit);
            } 
             if ( !ggWave->recevingRxData() && g_busyBlock == 1 )
            {
                std::cout << "Stop receiving\n";
                g_busyBlock = 0;
                const char *filename = "/tmp/inhibit";
                remove(filename);
            }
            /*  
             * Add to ggwave.h:
             * 
             * bool hasNewRxData() const { return m_rx.hasNewRxData; }
             * void resetNewRxFlag() { m_rx.hasNewRxData = false; }
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
                // Write received data to fifo
                // 
                // If no process is reading from the FIFO, open(FIFO_NAME_OUTPUT, O_WRONLY); blocks indefinitely,
                // you can try O_NONBLOCK
                // int fd = open(FIFO_NAME_OUTPUT, O_WRONLY | O_NONBLOCK);
                //
                int fd = open(FIFO_NAME_OUTPUT, O_WRONLY);
                if (fd == -1) {
                    printf("Error opening FIFO for writing \n");
                }
                write(fd, receivedData.data(), ggWave->getRxDataLength() );
                close(fd);
                ggWave->resetNewRxFlag();
            }
            
        }
    }
    inputThread.join();    
    GGWave_deinit();
    return 0;
}
