#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "curl/curl.h"

static size_t BYTES_GOTTEN = 0;
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    BYTES_GOTTEN += size * nmemb;
    // printf("Got some data of size %lu!\n", size * nmemb);
    return size * nmemb;
}

#define checkok(x) do { assert((x) == CURLE_OK); } while (0)

#define QUACK_SIZE 2048
static char LAST_QUACK[2 * QUACK_SIZE]; /* 2x for good luck */

static char *BINARY_NAME = "sidecurl";
static char *URL = NULL;
static FILE *BODY_INPUT_FILE = NULL;
static char *QUICHE_CC = NULL;
static char *SIDECAR_INTERFACE = NULL;
static int SIDECAR_THRESHOLD = 0;
static void parseargs(int argc, char **argv);

int main(int argc, char **argv) {
    parseargs(argc, argv);

    /*** OPEN A SIDECAR SOCKET ***/
    ssize_t n_bytes_quacked;
    int sidecar_socket = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5103);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    assert(!bind(sidecar_socket, (struct sockaddr *)&addr, sizeof(addr)));

    /*** OPEN A CURL HANDLE ***/
    CURL *easy_handle = curl_easy_init();
    // Set cURL options
    checkok(curl_easy_setopt(easy_handle, CURLOPT_URL, URL));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_POST, 1L));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_READDATA, BODY_INPUT_FILE));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYPEER, 0L));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_callback));
    if (QUICHE_CC)
        checkok(curl_easy_setopt(easy_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3));
    // Set sidecar options
    checkok(curl_easy_setopt(easy_handle, CURLOPT_QUICHE_CC, QUICHE_CC));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_SIDECAR_INTERFACE, SIDECAR_INTERFACE));
    checkok(curl_easy_setopt(easy_handle, CURLOPT_THRESHOLD, SIDECAR_THRESHOLD));

    CURLM *multi_handle = curl_multi_init();
    checkok(curl_multi_add_handle(multi_handle, easy_handle));

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;

    fd_set fdread, fdwrite, fdexcep;
    int n_transfers_running;
    do {
        /*** FILE DESCRIPTORS ***/
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);
        // Get from cURL
        int maxfd = -1;
        while (maxfd == -1) {
            checkok(curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd));
            if (maxfd == -1)
                curl_multi_perform(multi_handle, &n_transfers_running);
        }
        // Then add ours
        assert(sidecar_socket < FD_SETSIZE);
        FD_SET(sidecar_socket, &fdread);

        /*** SELECT ***/
        select(maxfd + 2, &fdread, &fdwrite, &fdexcep, &timeout);
        if (FD_ISSET(sidecar_socket, &fdread)) {
            n_bytes_quacked = recv(sidecar_socket, LAST_QUACK,
                                   QUACK_SIZE, MSG_DONTWAIT);
            if (n_bytes_quacked > 0) {
                LAST_QUACK[n_bytes_quacked] = '\0';
                printf("New quack: '%s' (recieved %lu maincar bytes)\n", LAST_QUACK, BYTES_GOTTEN);
            } else if (n_bytes_quacked < 0 && errno != EAGAIN) {
                perror("Error getting quack:");
                exit(1);
            }
        }
        curl_multi_perform(multi_handle, &n_transfers_running);
    } while (n_transfers_running);
    printf("Transfers done! %lu bytes\n", BYTES_GOTTEN);
    return 0;
}

void usage() {
    fprintf(stderr, "Usage: %s [URL] [path/to/post/body/file]\n", BINARY_NAME);
    fprintf(stderr, "Optional flags:\n");
    fprintf(stderr, "--quiche-cc    [reno|cubic|bbr]\n");
    fprintf(stderr, "--sidecar      [interface]\n");
    fprintf(stderr, "--threshold    [sidecar threshold]\n");
    fprintf(stderr, "HTTP3 is enabled exactly when --quiche-cc is specified\n");
    fprintf(stderr, "Result data is counted but ignored.\n");
    fprintf(stderr, "(--insecure is enabled automatically.)\n");
    exit(1);
}
void parseargs(int argc, char **argv) {
    if (argc == 0) usage();

    struct option options[] = {
        {"quiche-cc",   required_argument, 0, 'q'},
        {"sidecar",     required_argument, 0, 's'},
        {"threshold",   required_argument, 0, 't'},
        {0, 0, 0, 0},
    };
    while (1) {
        int c = getopt_long(argc, argv, "", options, NULL);
        if (c == -1) break;
        switch (c) {
        case 'q':
            QUICHE_CC = strdup(optarg);
            break;
        case 's':
            SIDECAR_INTERFACE = strdup(optarg);
            break;
        case 't':
            SIDECAR_THRESHOLD = atoi(optarg);
            break;
        case '?':
            exit(1);
            break;
        }
    }
    if ((optind + 2) != argc) usage();
    URL = argv[optind];
    BODY_INPUT_FILE = fopen(argv[optind + 1], "r");
    if (!BODY_INPUT_FILE) {
        printf("Error opening POST body file '%s'\n", argv[optind + 1]);
        perror("Error Message:");
    }
}
