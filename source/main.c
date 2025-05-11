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
#include "switch/crypto/sha1.h"
#include "jsmn.h"
#include <curl/curl.h>
#include <pthread.h>

#define CONFIG_FILE "config.txt"
#define BUFFER_SIZE 1024
#define SOFTWARE "nxducominer"
#define GET_POOL "https://server.duinocoin.com/getPool"

//                              bg m red gre blu 
#define DUCO_ORANGE CONSOLE_ESC(38;2;252;104;3m)
#define ERROR_RED CONSOLE_ESC(31m)
#define WARNING_ORANGE CONSOLE_ESC(38;2;255;165;0m)
#define NOTICE_BLUE CONSOLE_ESC(38;2;135;206;235m)
#define DARK_GREY CONSOLE_ESC(38;2;90;90;90m)
#define RESET CONSOLE_ESC(0m)

#define CONSOLE_ESC_NSTR(fmt) "\033[" fmt


#define SET_DYNAMIC_STRING(field) \
            do { \
                free(config->field); \
                config->field = strdup(value); \
                if (config->field == NULL) { \
                    cleanup("ERROR Memory allocation failed"); \
                } \
            } while(0)

void get_time_string(char* buffer, int size) {
    time_t rawtime = time(NULL);
    struct tm* timeinfo = localtime(&rawtime);
    strftime(buffer, size, "%H:%M:%S", timeinfo);
}

typedef struct {
    int socket_fd;
    int thread_id;
    float hashrate;
    int difficulty;
    int total_shares;
    int good_shares;
    int bad_shares;
    int blocks;
} ThreadData;

typedef struct {
    int last_share;
    u32 charge;
    s32 skinTempMilliC;
    pthread_mutex_t lock;
    pthread_t* miningThreads;
    ThreadData* threadData;
    int single_miner_id;
} ResourceManager;

ResourceManager res = {
   .last_share = 0,
   .charge = 0,
   .skinTempMilliC = 0,
   .lock = PTHREAD_MUTEX_INITIALIZER,
   .miningThreads = NULL,
   .threadData = NULL,
   .single_miner_id = 0
};

typedef struct {
    char* node;
    int port;
    char* wallet_address;
    char* miner_key;
    char* difficulty;
    char* rig_id;
    bool cpu_boost;
    bool iot;
    int threads;
} MiningConfig;

MiningConfig mc = {
   .node = NULL,
   .wallet_address = NULL,
   .miner_key = NULL,
   .difficulty = NULL,
   .rig_id = NULL,
   .port = 0,
   .cpu_boost = false,
   .iot = false,
   .threads = 1
};


void cleanup(char* msg) {
    if (msg == NULL) {
        if (mc.threads > 1) {
            printf(CONSOLE_ESC(80;1H) "Waiting for threads to exit...");
        }
        else {
            printf(CONSOLE_ESC(80;1H) "Exiting...");
        }
        consoleUpdate(NULL);
    }
    else {
        printf(ERROR_RED);
        printf(CONSOLE_ESC(80;1H) "%s. Exiting...", msg);
        printf(RESET);
        consoleUpdate(NULL);
    }
    sleep(3);

    //cleanup threads
    if (res.miningThreads != NULL) {
        if (res.threadData != NULL) {
            for (int i = 0; i < mc.threads; i++) {          //close sockets
                if (res.threadData[i].socket_fd >= 0) {
                    close(res.threadData[i].socket_fd);
                    res.threadData[i].socket_fd = -1;
                }
            }
        }
        for (int i = 0; i < mc.threads; i++) {              //cancel threads
            if (res.miningThreads[i]) {
                pthread_cancel(res.miningThreads[i]);
            }
        }
        for (int i = 0; i < mc.threads; i++) {              //join threads
            if (res.miningThreads[i]) {
                pthread_join(res.miningThreads[i], NULL);
            }
        }
    }

    // free resources
    free(res.miningThreads);
    free(res.threadData);
    free(mc.difficulty);
    free(mc.miner_key);
    free(mc.node);
    free(mc.rig_id);
    free(mc.wallet_address);
    
    psmExit();
    tcExit();
    socketExit();
    consoleExit(NULL);
    exit(0);
}

void parseConfigFile(MiningConfig* config) {
    printf("Reading %s\n", CONFIG_FILE);
    FILE* f = fopen(CONFIG_FILE, "r");
    if (f == NULL) {
        cleanup("Failed to open config file");
    }

    char line[100];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char* sep = strchr(line, ':');
        if (!sep) continue;

        *sep = '\0';
        char* key = line;
        char* value = sep + 1;

        while (*value == ' ') value++;

        printf("%s:%s\n", key, value);
        consoleUpdate(NULL);

        if (strcmp(key, "node") == 0) {
            SET_DYNAMIC_STRING(node);
        }
        else if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        }
        else if (strcmp(key, "wallet_address") == 0) {
            if (strlen(value) < 1)
                cleanup("ERROR wallet_address not set");
            SET_DYNAMIC_STRING(wallet_address);
        }
        else if (strcmp(key, "miner_key") == 0) {
            SET_DYNAMIC_STRING(miner_key);
        }
        else if (strcmp(key, "difficulty") == 0) {
            if (strlen(value) < 1)
                cleanup("ERROR difficulty not set");
            SET_DYNAMIC_STRING(difficulty);
        }
        else if (strcmp(key, "rig_id") == 0) {
            if (strlen(value) < 1)
                cleanup("ERROR rig_id not set");
            SET_DYNAMIC_STRING(rig_id);
        }
        else if (strcmp(key, "cpu_boost") == 0) {
            if (strlen(value) < 1)
                cleanup("ERROR cpu_boost not set");
            config->cpu_boost = (strcmp(value, "true") == 0) ? true : false;
        }
        else if (strcmp(key, "iot") == 0) {
            if (strlen(value) < 1)
                cleanup("ERROR iot not set");
            config->iot = (strcmp(value, "true") == 0) ? true : false;
        }
        else if (strcmp(key, "threads") == 0) {
            config->threads = atoi(value);
            if (config->threads < 1 || config->threads > 6)
                cleanup("ERROR threads value out of range");
        }
    }
    fclose(f);
    printf("File parsing completed");
}

struct MemoryStruct {
    char* memory;
    size_t size;
};

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        free(mem->memory);
        mem->memory = NULL;
        mem->size = 0;
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void getNode(char** ip, int* port) {
    printf(CONSOLE_ESC(2J));
    printf(CONSOLE_ESC(1;1H) "Finding a node from master server...");
    consoleUpdate(NULL);
    sleep(2);

    CURL* curl;
    CURLcode res;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);  // start with empty buffer
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (!curl) {
        cleanup("ERROR: curl init failed");
    }

    curl_easy_setopt(curl, CURLOPT_URL, GET_POOL);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libnx curl nxducominer");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        cleanup("ERROR: curl failed");
    }

    curl_easy_cleanup(curl);
    char* json_copy = strdup(chunk.memory);

    jsmn_parser parser;
    jsmntok_t tokens[13];
    jsmn_init(&parser);

    int ret = jsmn_parse(&parser, json_copy, strlen(json_copy), tokens, 13);
    if (ret < 0) {
        cleanup("ERROR: failed to parse JSON");
    }

    for (int i = 1; i < ret; i++) {
        if (tokens[i].type == JSMN_STRING) {
            if (strncmp(json_copy + tokens[i].start, "ip", tokens[i].end - tokens[i].start) == 0) {
                if (i + 1 < ret) {
                    char ip_str[16];
                    int length = tokens[i + 1].end - tokens[i + 1].start;
                    strncpy(ip_str, json_copy + tokens[i + 1].start, length);
                    *ip = malloc(length + 1);
                    strncpy(*ip, json_copy + tokens[i + 1].start, length);
                    (*ip)[length] = '\0';
                    i++;
                }
            }
            else if (strncmp(json_copy + tokens[i].start, "port", tokens[i].end - tokens[i].start) == 0) {
                if (i + 1 < ret) {
                    char port_str[16];
                    int length = tokens[i + 1].end - tokens[i + 1].start;
                    strncpy(port_str, json_copy + tokens[i + 1].start, length);
                    port_str[length] = '\0';
                    *port = atoi(port_str);
                    i++;
                }
            }
        }
    }

    printf(CONSOLE_ESC(3;1H)"Using %s:%i...", mc.node, mc.port);
    consoleUpdate(NULL);
    sleep(2);

    curl_global_cleanup();
    free(chunk.memory);
    free(json_copy);
}

void* doMiningWork(void* arg) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    ThreadData* td = (ThreadData*)arg;
    char recv_buf[BUFFER_SIZE];
    td->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (td->socket_fd < 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ERROR thread %d failed to create socket", td->thread_id);
        cleanup(buf);

        return NULL;
    }

    struct hostent* server = gethostbyname(mc.node);
    if (!server) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ERROR thread %d socket no such host", td->thread_id);
        cleanup(buf);
        return NULL;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(mc.port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(td->socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ERROR thread %d failed to connect", td->thread_id);
        cleanup(buf);
        return NULL;
    }
    read(td->socket_fd, recv_buf, sizeof(4));

    while (1) {
        pthread_testcancel();

        char iot[26];
        snprintf(iot, sizeof(iot), "Charge:%u%%@Temp:%.2f*C", res.charge, res.skinTempMilliC / 1000.0f);

        // request job
        char job_request[128];
        if (mc.iot && td->thread_id == 0) { //only send iot information on one thread
            snprintf(job_request, sizeof(job_request),
                "JOB,%s,%s,%s,%s",
                mc.wallet_address, mc.difficulty, mc.miner_key, iot);
        }
        else {
            snprintf(job_request, sizeof(job_request),
                "JOB,%s,%s,%s",
                mc.wallet_address, mc.difficulty, mc.miner_key);
        }

        write(td->socket_fd, job_request, strlen(job_request));

        // receive job
        memset(recv_buf, 0, BUFFER_SIZE);
        int n = read(td->socket_fd, recv_buf, BUFFER_SIZE - 1);
        if (n <= 0) break;

        // split job parts
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

        // initialize sha1 context
        Sha1Context base_ctx;
        Sha1Context temp_ctx;
        sha1ContextCreate(&base_ctx);
        sha1ContextUpdate(&base_ctx, (const unsigned char*)base_str, strlen(base_str));

        time_t start_time = time(NULL);
        char result_hash[41];
        int nonce;

        for (nonce = 0; nonce <= (100 * difficulty + 1); nonce++) {
            pthread_testcancel();

            unsigned char hash[20];
            char result_str[16];

            temp_ctx = base_ctx;  // copy base_ctx to temp_ctx
            int len = sprintf(result_str, "%d", nonce);
            sha1ContextUpdate(&temp_ctx, (const unsigned char*)result_str, len);
            sha1ContextGetHash(&temp_ctx, hash);

            // compare hash
            for (int i = 0; i < 20; i++) {
                snprintf(result_hash + (i * 2), 3, "%02x", hash[i]);
            }

            if (memcmp(result_hash, expected_hash, 20) == 0) {
                double elapsed = difftime(time(NULL), start_time);
                double hashrate = nonce / (elapsed > 0 ? elapsed : 1);

                // send result
                char submit_buf[128];
                int len = snprintf(submit_buf, sizeof(submit_buf), "%d,%.2f,%s,%s,,%i",
                    nonce, hashrate, SOFTWARE, mc.rig_id, res.single_miner_id);
                write(td->socket_fd, submit_buf, len);

                // read response
                read(td->socket_fd, recv_buf, BUFFER_SIZE - 1);

                if (strncmp(recv_buf, "GOOD", 4) == 0) td->good_shares++;
                else if (strncmp(recv_buf, "BLOCK", 5) == 0) td->blocks++;
                else td->bad_shares++;

                res.last_share = nonce;
                td->difficulty = difficulty;
                td->hashrate = hashrate / 1000.0f;
                td->total_shares++;

                break;
            }
        }
    }
    if (td->socket_fd >= 0) {
        close(td->socket_fd);
        td->socket_fd = -1;
    }
    return NULL;
}
int main() {
    consoleInit(NULL);

    // set up joycons
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // prevent sleeping
    appletSetAutoSleepDisabled(true);
    appletSetTvPowerStateMatchingMode(AppletTvPowerStateMatchingMode_Unknown1);

    socketInitializeDefault();
    nxlinkStdio();

    // parse config
    parseConfigFile(&mc);
    consoleUpdate(NULL);
    sleep(1);

    // find a node if node not set
    if (mc.node == NULL || mc.port == 0) {
        getNode(&mc.node, &mc.port);
    }

    // toggle CPU boost
    if (mc.cpu_boost) {
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }

    // init temperature
    Result tcrc = tcInitialize();
    if (R_FAILED(tcrc)) {
        cleanup("ERROR: failed to initialize tc");
    }

    // init battery management
    Result psmrc = psmInitialize();
    if (R_FAILED(psmrc)) {
        cleanup("ERROR: failed to initialize psm");
    }

    // allocate thread data
    res.miningThreads = malloc(mc.threads * sizeof(pthread_t));
    res.threadData = malloc(mc.threads * sizeof(ThreadData));
    srand(time(NULL));
    res.single_miner_id = rand() % 2812;

    if (!res.miningThreads || !res.threadData) {
        cleanup("ERROR: Memory allocation failed");
    }

    // create mining threads
    for (int i = 0; i < mc.threads; i++) {
        res.threadData[i].hashrate = 0.0f;
        res.threadData[i].difficulty = 0;
        res.threadData[i].bad_shares = 0;
        res.threadData[i].good_shares = 0;
        res.threadData[i].total_shares = 0;
        res.threadData[i].blocks = 0;
        res.threadData[i].thread_id = i;
        res.threadData[i].socket_fd = -1; 
        if (pthread_create(&res.miningThreads[i], NULL, doMiningWork, (void*)&res.threadData[i]) != 0) {
            cleanup("ERROR: Failed to start mining thread");
        }
        sleep(1); // stagger thread creation
    }

    char timebuf[16];
    time_t lastDraw = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) {
            cleanup(NULL);
            break;
        }
        time_t currentTime;
        time(&currentTime);

        if (difftime(currentTime, lastDraw) >= 2) {
            get_time_string(timebuf, sizeof(timebuf));

            pthread_mutex_lock(&res.lock);
            psmrc = psmGetBatteryChargePercentage(&res.charge);
            tcrc = tcGetSkinTemperatureMilliC(&res.skinTempMilliC);

            float total_hashrate = 0.0f;
            int total_difficulty = 0;
            float avg_difficulty = 0.0f;
            int total_shares = 0;
            int good_shares = 0;
            int bad_shares = 0;
            int blocks = 0;
            for (int i = 0; i < mc.threads; i++) {
                total_hashrate += res.threadData[i].hashrate;
                total_difficulty += res.threadData[i].difficulty;
                total_shares += res.threadData[i].total_shares;
                good_shares += res.threadData[i].good_shares;
                bad_shares += res.threadData[i].bad_shares;
                blocks += res.threadData[i].blocks;
            }
            avg_difficulty = (float)total_difficulty / mc.threads;

            printf(CONSOLE_ESC(2J)); // clear screen

            printf(CONSOLE_ESC(1;1H) NOTICE_BLUE "Press [+] to exit..." RESET);
            printf(CONSOLE_ESC(2;1H) "Connected to %s:%i", mc.node, mc.port);
            printf(CONSOLE_ESC(3;1H) "Current Time: %s", timebuf);

            if (res.charge > 25) {
                printf(CONSOLE_ESC(4;1H) "Battery charge: %u%%", res.charge);
            }
            else {
                printf(CONSOLE_ESC(4;1H) ERROR_RED "Battery charge: %u%%" RESET, res.charge);
            }

            if (res.skinTempMilliC / 1000.0f > 55.0f) {
                printf(CONSOLE_ESC(5;1H) ERROR_RED "Temperature: %.2f C" RESET, res.skinTempMilliC / 1000.0f);
            }
            else {
                printf(CONSOLE_ESC(5;1H) "Temperature: %.2f C", res.skinTempMilliC / 1000.0f);
            }
            // row 6 lb
            printf(CONSOLE_ESC(7;1H)  "Rig ID: %s", mc.rig_id);
            printf(CONSOLE_ESC(9;1H)  "Hashrate: %.2f kH/s %s", total_hashrate, mc.cpu_boost ? "(CPU Boosted)" : "");
            printf(CONSOLE_ESC(10;1H) "Difficulty: %d", (int)avg_difficulty);
            // row 11 lb
            printf(CONSOLE_ESC(12;1H) "Shares");
            printf(CONSOLE_ESC(13;1H) "|_ Last share: %i", res.last_share);
            printf(CONSOLE_ESC(14;1H) "|_ Total: %i", total_shares);
            printf(CONSOLE_ESC(15;1H) "|_ Accepted: %i", good_shares);
            printf(CONSOLE_ESC(16;1H) "|_ Rejected: %i", bad_shares);
            printf(CONSOLE_ESC(17;1H) "|_ Accepted %i/%i Rejected (%d%% Accepted)", good_shares, bad_shares, (int)((double)good_shares / total_shares * 100));
            printf(CONSOLE_ESC(18;1H) "|_ Blocks Found: %i", blocks);

            pthread_mutex_unlock(&res.lock);

            //thread info
            if (mc.threads > 1) {
                printf(CONSOLE_ESC(20;1H) "Threads (%i)", mc.threads);
                int startLine = 21;
                for (int i = 0; i < mc.threads; i++) {
                    printf(CONSOLE_ESC_NSTR("%d;1H") "%i|_ Hashrate: %.2f kH/s", startLine + (i * 4), i, res.threadData[i].hashrate);
                    printf(CONSOLE_ESC_NSTR("%d;1H") " |_ Difficulty: %i", startLine + (i * 4) + 1, res.threadData[i].difficulty);
                    printf(CONSOLE_ESC_NSTR("%d;1H") " |_ Accepted %i/%i Rejected", startLine + (i * 4) + 2, res.threadData[i].good_shares, res.threadData[i].bad_shares);
                }
            }

            //logo
            printf(DUCO_ORANGE);
            printf(CONSOLE_ESC(1;53H)  "         ########          ");
            printf(CONSOLE_ESC(2;53H)  "      ###############      ");
            printf(CONSOLE_ESC(3;53H)  "    ###################    ");
            printf(CONSOLE_ESC(4;53H)  "   #####         #######   ");
            printf(CONSOLE_ESC(5;53H)  "  #############    ######  ");
            printf(CONSOLE_ESC(6;53H)  " #######       ###   ##### ");
            printf(CONSOLE_ESC(7;53H)  " ############   ##   ##### ");
            printf(CONSOLE_ESC(8;53H)  " ############   ##   ##### ");
            printf(CONSOLE_ESC(9;53H)  " #######       ###   ##### ");
            printf(CONSOLE_ESC(10;53H) "  #############    ######  ");
            printf(CONSOLE_ESC(11;53H) "   #####         #######   ");
            printf(CONSOLE_ESC(12;53H) "    ###################    ");
            printf(CONSOLE_ESC(13;53H) "      ###############      ");
            printf(CONSOLE_ESC(14;53H) "          #######          ");
            printf(RESET);
            printf(CONSOLE_ESC(15;52H) "github.com/tbwcjw/nxducominer");

            //version string
            printf(CONSOLE_ESC(80;67H) DARK_GREY "%s" RESET, APP_VERSION);

            lastDraw = currentTime;
            pthread_mutex_unlock(&res.lock);
            consoleUpdate(NULL);
        }
        svcSleepThread(1000000);
    }
}

