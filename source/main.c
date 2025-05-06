#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <switch.h>
#include "sha1.h"

#define CONFIG_FILE "config.txt"
#define BUFFER_SIZE 1024

#define DUCO_ORANGE CONSOLE_ESC(38;2;252;104;3m)
#define ERROR_RED CONSOLE_ESC(31m)
#define NOTICE_BLUE CONSOLE_ESC(38;2;135;206;235m)
#define RESET CONSOLE_ESC(0m)

void sha1_string(const char* input, char* output) {
    SHA1_CTX ctx;
    unsigned char hash[20];
    SHA1Init(&ctx);
    SHA1Update(&ctx, (const unsigned char*)input, strlen(input));
    SHA1Final(hash, &ctx);
    for (int i = 0; i < 20; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
}

void get_time_string(char* buffer, int size) {
    time_t rawtime = time(NULL);
    struct tm* timeinfo = localtime(&rawtime);
    strftime(buffer, size, "%H:%M:%S", timeinfo);
}

typedef struct {
    int socket_fd;
    bool console_initialized;
    bool socket_initialized;
    int last_share;
    int difficulty;
    double hashrate;
    int good_shares;
    int bad_shares;
    int blocks;
    int total_shares;
    char* timebuf;
} ResourceManager;

typedef struct {
    char* node;
    int port;
    char* wallet_address;
    char* miner_key;
    char* difficulty;
    char* rig_id;
} MiningConfig;



void cleanup(ResourceManager* res, char* msg) {
    if (msg == NULL) {
        printf("\nExiting...");
        consoleUpdate(NULL);
    }
    else {
        printf(ERROR_RED);
        printf(msg);
        printf(RESET);
        consoleUpdate(NULL);
    }
    
    sleep(5);
    if (res->socket_fd >= 0) {
        close(res->socket_fd);
        res->socket_fd = -1;
    }

    if (res->socket_initialized) {
        socketExit();
        res->socket_initialized = false;
    }

    if (res->console_initialized) {
        consoleUpdate(NULL); 
        consoleExit(NULL);
        res->console_initialized = false;
    }
}

void parseConfigFile(MiningConfig* config) {
    printf("Reading %s\n", CONFIG_FILE);
    FILE* f = fopen(CONFIG_FILE, "r");
    if (f == NULL) {
        cleanup(NULL, "Failed to open config file");
    }

    char line[100];
    while (fgets(line, sizeof(line), f) != NULL) {

        line[strcspn(line, "\r\n")] = '\0';
        char* sep = strchr(line, ':');
        if (!sep) continue; 

        *sep = '\0';
        char* key = line;
        char* value = sep + 1;

        printf("%s:%s\n", key, value);
        consoleUpdate(NULL);

        #define SET_DYNAMIC_STRING(field) \
            do { \
                free(config->field); \
                config->field = strdup(value); \
                if (config->field == NULL) { \
                    fprintf(stderr, "Memory allocation failed for %s\n", #field); \
                    exit(EXIT_FAILURE); \
                } \
            } while(0)

        if (strcmp(key, "node") == 0) {
            SET_DYNAMIC_STRING(node);
        }
        else if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        }
        else if (strcmp(key, "wallet_address") == 0) {
            SET_DYNAMIC_STRING(wallet_address);
        }
        else if (strcmp(key, "miner_key") == 0) {
            SET_DYNAMIC_STRING(miner_key);
        }
        else if (strcmp(key, "difficulty") == 0) {
            SET_DYNAMIC_STRING(difficulty);
        }
        else if (strcmp(key, "rig_id") == 0) {
            SET_DYNAMIC_STRING(rig_id);
        }
    }
    fclose(f);
    printf("File parsing completed\n");
}

int main() {
    char timebuf[16];

    //initialize console
    ResourceManager res = {
    .socket_fd = -1,
    .console_initialized = false,
    .socket_initialized = false,
    .last_share = 0,
    .difficulty = 0,
    .hashrate = 0,
    .good_shares = 0,
    .bad_shares = 0,
    .blocks = 0,
    .total_shares = 0,
    .timebuf = 0
        };
    consoleInit(NULL);
    res.console_initialized = true;
    socketInitializeDefault();
    res.socket_initialized = true;

    //redirect stdio to nxlink server
    //nxlinkStdio();

    //parse config
    MiningConfig mc = {
    .node = NULL,
    .wallet_address = NULL,
    .miner_key = NULL,
    .difficulty = NULL,
    .rig_id = NULL,
    .port = 0
    };
    parseConfigFile(&mc);
    consoleUpdate(NULL);
    sleep(1);

    //set up joycons
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    //battery management
    Result rc = psmInitialize();
    if (R_FAILED(rc)) {
        consoleUpdate(NULL);
        sleep(5);
        cleanup(&res, "ERROR: failed to initalize psm");
        return 0;
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) {
            cleanup(&res, NULL);
            return 0;
        }

        printf("Connecting to %s:%d\n", mc.node, mc.port);
        consoleUpdate(NULL);

        res.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct hostent* server = gethostbyname(mc.node);

        if (!server) {
            consoleUpdate(NULL);
            sleep(5);
            cleanup(&res, "ERROR: No such host");
            continue;
        }

        struct sockaddr_in serv_addr;

        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(mc.port);
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

        if (connect(res.socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            consoleUpdate(NULL);
            sleep(5);
            cleanup(&res, "ERROR connecting");
            continue;
        }

        char recv_buf[BUFFER_SIZE];
        read(res.socket_fd, recv_buf, sizeof(4)); // server version, 4 bytes
        get_time_string(timebuf, sizeof(timebuf));
        consoleUpdate(NULL);

        while (1) {
            char job_request[128];
            snprintf(job_request, sizeof(job_request), "JOB,%s,%s,%s", mc.wallet_address, mc.difficulty, mc.miner_key);
            write(res.socket_fd, job_request, strlen(job_request));

            memset(recv_buf, 0, BUFFER_SIZE);
            int n = read(res.socket_fd, recv_buf, BUFFER_SIZE - 1);
            if (n <= 0) break;

            char* job_parts[3];
            char* token = strtok(recv_buf, ",");
            for (int i = 0; i < 3 && token; i++) {
                job_parts[i] = token;
                token = strtok(NULL, ",");
            }

            int difficulty = atoi(job_parts[2]);
            char base_str[128];
            char expected_hash[41];
            strcpy(base_str, job_parts[0]);
            strcpy(expected_hash, job_parts[1]);

            SHA1_CTX base_ctx;
            SHA1Init(&base_ctx);
            SHA1Update(&base_ctx, (unsigned char*)base_str, strlen(base_str));

            time_t start_time = time(NULL);
            char result_hash[41];
            int result;
            for (result = 0; result <= 100 * difficulty; result++) {
                padUpdate(&pad);
                u64 kDown = padGetButtonsDown(&pad);

                if (kDown & HidNpadButton_Plus) {
                    cleanup(&res, NULL);
                    break;
                }

                SHA1_CTX temp_ctx = base_ctx;
                char result_str[16];
                sprintf(result_str, "%d", result);
                SHA1Update(&temp_ctx, (unsigned char*)result_str, strlen(result_str));
                unsigned char hash[20];
                SHA1Final(hash, &temp_ctx);

                for (int i = 0; i < 20; i++) {
                    sprintf(result_hash + (i * 2), "%02x", hash[i]);
                }
                result_hash[40] = '\0';

                if (strcmp(result_hash, expected_hash) == 0) {
                    double elapsed = difftime(time(NULL), start_time);
                    double hashrate = result / (elapsed > 0 ? elapsed : 1);

                    char submit_buf[128]; //send result
                    snprintf(submit_buf, sizeof(submit_buf), "%d,%.2f,%s", result, hashrate, mc.rig_id);
                    write(res.socket_fd, submit_buf, strlen(submit_buf));

                    read(res.socket_fd, recv_buf, BUFFER_SIZE - 1);
                    get_time_string(timebuf, sizeof(timebuf));
                    if (strncmp(recv_buf, "GOOD", 4) == 0) {
                        res.good_shares++;
                    }
                    else if (strncmp(recv_buf, "BAD", 3) == 0) {
                        res.bad_shares++;
                    }
                    else if (strncmp(recv_buf, "BLOCK", 5) == 0) {
                        res.blocks++;
                    }

                    u32 charge;
                    rc = psmGetBatteryChargePercentage(&charge);

                    res.last_share = result;
                    res.difficulty = difficulty;
                    res.timebuf = timebuf;
                    res.hashrate = hashrate / 1000.0;
                    res.total_shares++;

                    printf(CONSOLE_ESC(2J)); //clear
                    //summary view
                    printf(CONSOLE_ESC(1;1H) NOTICE_BLUE "Press [+] to exit..." RESET);
                    printf(CONSOLE_ESC(2;1H) "Connected to %s:%i", mc.node, mc.port);
                    printf(CONSOLE_ESC(3;1H) "Current Time: %s", res.timebuf);
                    printf(CONSOLE_ESC(4;1H) "Battery charge: %u%%", charge);
                    // row 5 lb
                    printf(CONSOLE_ESC(6;1H) "Rig ID: %s", mc.rig_id);
                    printf(CONSOLE_ESC(7;1H) "Hashrate: %.2f kH/s", res.hashrate);
                    printf(CONSOLE_ESC(8;1H) "Difficulty: %i", res.difficulty);
                    // row 9 lb
                    printf(CONSOLE_ESC(10;1H) "Shares");
                    printf(CONSOLE_ESC(11;1H) "|_ Last share: %i", res.last_share);
                    printf(CONSOLE_ESC(12;1H) "|_ Total: %i", res.total_shares);
                    printf(CONSOLE_ESC(13;1H) "|_ Accepted: %i", res.good_shares);
                    printf(CONSOLE_ESC(14;1H) "|_ Rejected: %i", res.bad_shares);
                    printf(CONSOLE_ESC(15;1H) "|_ Accepted %i/%i Rejected (%d%% Accepted)", res.good_shares, res.bad_shares, (int)((double)res.good_shares / res.total_shares * 100));
                    printf(CONSOLE_ESC(16;1H) "|_ Blocks Found: %i", res.blocks);

                    //logo

                    //                 bg m red gre blu 
                    printf(DUCO_ORANGE);
                    printf(CONSOLE_ESC(1;52H)  "         ##########          ");
                    printf(CONSOLE_ESC(2;52H)  "      #################      ");
                    printf(CONSOLE_ESC(3;52H)  "    #####################    ");
                    printf(CONSOLE_ESC(4;52H)  "   ######         ########   ");
                    printf(CONSOLE_ESC(5;52H)  "  ##############    #######  ");
                    printf(CONSOLE_ESC(6;52H)  " ########       ###   ###### ");
                    printf(CONSOLE_ESC(7;52H)  " #############   ##   ###### ");
                    printf(CONSOLE_ESC(8;52H)  " #############   ##   ###### ");
                    printf(CONSOLE_ESC(9;52H)  " ########       ###   ###### ");
                    printf(CONSOLE_ESC(10;52H) "  ##############    #######  ");
                    printf(CONSOLE_ESC(11;52H) "   ######         ########   ");
                    printf(CONSOLE_ESC(12;52H) "    #####################    ");
                    printf(CONSOLE_ESC(13;52H) "      #################      ");
                    printf(CONSOLE_ESC(14;52H) "          #########          ");
                    printf(RESET);
                    printf(CONSOLE_ESC(15;52H) "github.com/tbwcjw/nxducominer");

                    consoleUpdate(NULL);
                    break;
                }
            }
        }
    }
}