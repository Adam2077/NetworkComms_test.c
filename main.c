#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <pthread.h>
#include "lib/mpack/mpack.h"

#pragma comment(lib, "Ws2_32.lib")

#define PORT 3360
#define BUFFER_SIZE 1024

typedef struct {
    char message[256];
    time_t timestamp;
    char additionalData[3][2];
} Objekt;

volatile int server_bezi = 1;

void* zpracuj_klienta(void* arg);
void* vlakno_serveru(void* arg);
void spust_server();
void spust_klient(const char* serverIP);
void odesli_objekt(SOCKET socket, Objekt* obj);
void prijmi_objekt(SOCKET socket, Objekt* obj);

int main() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup selhal: %d\n", iResult);
        return 1;
    }

    char volba[2];

    printf("Vyberte režim: 1) Server 2) Klient\n");
    fgets(volba, sizeof(volba), stdin);

    if (strcmp(volba, "1") == 0) {
        spust_server();
    } else if (strcmp(volba, "2") == 0) {
        char serverIP[16];
        printf("Zadejte IP adresu serveru\n");
        scanf("%15s", serverIP);
        spust_klient(serverIP);
    } else {
        printf("Neplatná volba.\n");
    }

    WSACleanup();
    return 0;
}

void spust_server() {
    SOCKET server_fd;
    struct sockaddr_in adresa;
    pthread_t server_vlakno;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        perror("Chyba při vytváření socketu");
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    adresa.sin_family = AF_INET;
    adresa.sin_addr.s_addr = INADDR_ANY;
    adresa.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&adresa, sizeof(adresa)) == SOCKET_ERROR) {
        perror("Chyba při bindování");
        closesocket(server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        perror("Chyba při naslouchání");
        closesocket(server_fd);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    printf("Server naslouchá na portu %d\n", PORT);
    printf("Stiskněte Enter pro ukončení.\n");

    pthread_create(&server_vlakno, NULL, vlakno_serveru, &server_fd);

    getchar();

    server_bezi = 0;
    closesocket(server_fd);
    pthread_join(server_vlakno, NULL);

    printf("Server ukončen.\n");
}

void* vlakno_serveru(void* arg) {
    SOCKET server_fd = *(SOCKET*)arg;
    SOCKET new_socket;
    struct sockaddr_in adresa;
    int addrlen = sizeof(adresa);

    while (server_bezi) {
        if ((new_socket = accept(server_fd, (struct sockaddr*)&adresa, &addrlen)) == INVALID_SOCKET) {
            perror("Chyba při přijímání připojení");
            continue;
        }

        printf("Připojen nový klient.\n");

        pthread_t klient_vlakno;
        SOCKET* pclient = malloc(sizeof(SOCKET));
        *pclient = new_socket;
        pthread_create(&klient_vlakno, NULL, zpracuj_klienta, pclient);
    }

    return NULL;
}

void* zpracuj_klienta(void* arg) {
    SOCKET socket = *(SOCKET*)arg;
    free(arg);

    Objekt prijatyObjekt;
    while (server_bezi) {
        prijmi_objekt(socket, &prijatyObjekt);
        printf("Přijatý objekt: Message: %s, Timestamp: %ld\n", prijatyObjekt.message, prijatyObjekt.timestamp);

        odesli_objekt(socket, &prijatyObjekt);
    }

    closesocket(socket);
    return NULL;
}

void spust_klient(const char* serverIP) {
    SOCKET sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("\n Chyba při vytváření socketu \n");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, serverIP, &serv_addr.sin_addr) <= 0) {
        printf("\nNeplatná adresa / Adresa není podporována \n");
        return;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        printf("\nPřipojení selhalo \n");
        return;
    }

    printf("Stiskněte Enter pro ukončení.\n");
    getchar();

    closesocket(sock);
}

void odesli_objekt(SOCKET socket, Objekt* obj) {
    char buffer[BUFFER_SIZE];
    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer, sizeof(buffer));

    mpack_start_map(&writer, 3);
    mpack_write_cstr(&writer, "message");
    mpack_write_cstr(&writer, obj->message);
    mpack_write_cstr(&writer, "timestamp");
    mpack_write_u64(&writer, (uint64_t)obj->timestamp);
    mpack_write_cstr(&writer, "additionalData");
    mpack_start_array(&writer, 3);
    for (int i = 0; i < 3; ++i) {
        mpack_write_cstr(&writer, obj->additionalData[i]);
    }
    mpack_finish_array(&writer);
    mpack_finish_map(&writer);

    send(socket, buffer, mpack_writer_buffer_used(&writer), 0);
}

void prijmi_objekt(SOCKET socket, Objekt* obj) {
    char buffer[BUFFER_SIZE];
    int len = recv(socket, buffer, sizeof(buffer), 0);
    if (len <= 0) {
        return;
    }

    mpack_tree_t tree;
    mpack_tree_init_data(&tree, buffer, len);
    mpack_tree_parse(&tree);

    mpack_node_t root = mpack_tree_root(&tree);
    mpack_node_t messageNode = mpack_node_map_cstr(root, "message");
    mpack_node_t timestampNode = mpack_node_map_cstr(root, "timestamp");
    mpack_node_t additionalDataNode = mpack_node_map_cstr(root, "additionalData");

    strncpy(obj->message, mpack_node_str(messageNode), sizeof(obj->message) - 1);
    obj->timestamp = (time_t)mpack_node_u64(timestampNode);

    for (int i = 0; i < mpack_node_array_length(additionalDataNode); ++i) {
        strncpy(obj->additionalData[i], mpack_node_str(mpack_node_array_at(additionalDataNode, i)), sizeof(obj->additionalData[i]) - 1);
    }

    mpack_tree_destroy(&tree);
}
