/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP_PORT 4242
#define BUF_SIZE 2048
#define POLL_TIME_S 5

int connection = 0;

typedef struct TCP_CLIENT_T {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    uint8_t buffer_recv[BUF_SIZE];
    int recv_len;
    int con_num;
} TCP_CLIENT_T;

static err_t tcp_server_close(struct tcp_pcb* server_pcb) {
    if (server_pcb) {
        tcp_arg(server_pcb, NULL);
        tcp_close(server_pcb);
    }
    return ERR_OK;
}


static err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    printf("tcp_server_sent %u\n", len);
    return ERR_OK;
}

err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, char *data, u16_t len)
{
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;

    printf("Writing %ld bytes to client %i\n", len, state->con_num);
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    err_t err = tcp_write(tpcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Failed to write data %d\n", err);
        //handle it
    }
    return ERR_OK;
}

err_t tcp_server_client_close(struct TCP_CLIENT_T* state){
    tcp_arg(state->client_pcb, NULL);
    tcp_poll(state->client_pcb, NULL, 0);
    tcp_sent(state->client_pcb, NULL);
    tcp_recv(state->client_pcb, NULL);
    tcp_err(state->client_pcb, NULL);
    err_t err = tcp_close(state->client_pcb);
    if (err != ERR_OK) {
        printf("close failed %d, calling abort\n", err);
        tcp_abort(state->client_pcb);
        err = ERR_ABRT;
    }
    return err;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;

    //this is the connection close
    if (!p) {
        //handle it
        tcp_server_client_close(state);
        free(state);
        return ERR_OK;
    }
    printf("Recieving from %i\n", state->con_num);
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        printf("tcp_server_recv %d/%d err %d\n", p->tot_len, state->recv_len, err);

        // Receive the buffer
        state->recv_len = pbuf_copy_partial(p, state->buffer_recv,p->tot_len > BUF_SIZE ? BUF_SIZE : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    for (int i = 0; i < p->tot_len; ++i) {
        printf("%c",state->buffer_recv[i]);
    }

    static char* str = "HTTP/1.1 200 OK\nContent-Length: 12\nContent-Type: text/plain; charset=utf-8\n\nHello World!";
    tcp_server_send_data(arg, state->client_pcb, str, strlen(str));

    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    printf("Client %i still connected\n", state->con_num);
    return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        printf("tcp_client_err_fn %d\n", err);
        //handle it
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {  //This is the only function that has a tcp_pcb callback
    struct tcp_pcb *server_pcb = (struct tcp_pcb*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        printf("Failure in accept\n");
        //handle it
        return ERR_VAL;
    }
    printf("Client connected\n");

    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));

    state->server_pcb = server_pcb;
    state->con_num = ++connection;
    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static struct tcp_pcb* tcp_server_open() {
    printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("failed to create pcb\n");
        return (struct tcp_pcb*) 0;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        printf("failed to bind to port %d\n");
        return (struct tcp_pcb*) 0;
    }

    struct tcp_pcb* server_pcb = tcp_listen_with_backlog(pcb, 10);   //backlog, how many connections which are not yet accepted
    if (!server_pcb) {
        printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return (struct tcp_pcb*) 0;
    }

    tcp_arg(server_pcb, server_pcb);  //what to give the callback func.
    tcp_accept(server_pcb, tcp_server_accept);   //tcp accept callback func.

    return server_pcb;
}


int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }

    volatile struct tcp_pcb *server_pcb = tcp_server_open();
    while(1){
        sleep_ms(1000);
    }
    tcp_server_close(server_pcb);

    cyw43_arch_deinit();
    return 0;
}