/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "websites/websites.h"

#define TCP_PORT 4242
#define BUF_SIZE 2048
#define POLL_TIME_S 5

uint32_t connection = 0;
uint16_t currently_connected = 0;

typedef struct TCP_CLIENT_T {
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

    currently_connected--;
    printf("Closed connection to %d, brk at %p\n",state->con_num, sbrk(0));

    return err;
}

err_t http_serve_response(struct TCP_CLIENT_T* client){     //do not call me, we need to be in an interrupt context


    //----------------- index ----------------------------------
    if(memcmp("GET / HTTP", client->buffer_recv, 10) == 0) {

        //build header
        sprintf(httpheader + (httpheader_LEN - 7), "%05i", indexpage_LEN); //write the content length
        httpheader[httpheader_LEN - 2] = 0xa;                           //remove the \0 string terminator
        httpheader[httpheader_LEN - 1] = 0xa;

        //send header
        tcp_server_send_data(client, client->client_pcb, httpheader, httpheader_LEN);

        //send website
        tcp_server_send_data(client, client->client_pcb, indexpage, indexpage_LEN);
        return ERR_OK;
    }

    //----------------- process color --------------------------
    if(memcmp("GET /post?color=%", client->buffer_recv, 17) == 0) {
        char color[7] = {0};
        memcpy(color, client->buffer_recv + 19, 6); //copy 6 chars, has \0 automatically
        printf("Color: #%s\n", color);
        uint8_t red = (color[0] >= 'a' ? (color[0] - 87)*16 : (color[0] - 48)*16) + (color[1] >= 'a' ? (color[1] - 87) : (color[1] - 48));
        uint8_t gre = (color[2] >= 'a' ? (color[2] - 87)*16 : (color[2] - 48)*16) + (color[3] >= 'a' ? (color[3] - 87) : (color[3] - 48));
        uint8_t blu = (color[4] >= 'a' ? (color[4] - 87)*16 : (color[4] - 48)*16) + (color[5] >= 'a' ? (color[5] - 87) : (color[5] - 48));

        printf("R: %x, G: %x, B: %x\n",red,gre,blu);

        //build header
        sprintf(httpheader + (httpheader_LEN - 7), "%05i", colorok_LEN); //write the content length
        httpheader[httpheader_LEN - 2] = 0xa;                           //remove the \0 string terminator
        httpheader[httpheader_LEN - 1] = 0xa;

        //send header
        tcp_server_send_data(client, client->client_pcb, httpheader, httpheader_LEN);

        //send website
        tcp_server_send_data(client, client->client_pcb, colorok, colorok_LEN);
        return ERR_OK;
    }

    //----------------- colorpicker --------------------------
    if(memcmp("GET /colorpicker HTTP", client->buffer_recv, 21) == 0) {

        //build header
        sprintf(httpheader + (httpheader_LEN - 7), "%05i", colorpicker_LEN); //write the content length
        httpheader[httpheader_LEN - 2] = 0xa;                           //remove the \0 string terminator
        httpheader[httpheader_LEN - 1] = 0xa;

        //send header
        tcp_server_send_data(client, client->client_pcb, httpheader, httpheader_LEN);

        //send website
        tcp_server_send_data(client, client->client_pcb, colorpicker, colorpicker_LEN);
        return ERR_OK;
    }

    //--------------- not found ------------------------------
    //build header
    sprintf(httpheader + (httpheader_LEN - 7), "%05i", notfound_LEN); //write the content length
    httpheader[httpheader_LEN - 2] = 0xa;                           //remove the \0 string terminator
    httpheader[httpheader_LEN - 1] = 0xa;

    //send header
    tcp_server_send_data(client, client->client_pcb, httpheader, httpheader_LEN);

    //send website
    tcp_server_send_data(client, client->client_pcb, notfound, notfound_LEN);
    return ERR_OK;

}


err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;

    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();

    //this is the connection close
    if (!p) {
        tcp_server_client_close(state);
        free(state);
        return ERR_OK;
    }

    printf("Receiving from %i\n", state->con_num);
    if (p->tot_len > 0) {
        printf("tcp_server_recv %d/%d err %d\n", p->tot_len, state->recv_len, err);

        // Receive the buffer
        state->recv_len = pbuf_copy_partial(p, state->buffer_recv,p->tot_len > BUF_SIZE ? BUF_SIZE : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);

    //print it (can we receive 0x00? if not we can use printf)
    for (int i = 0; i < p->tot_len; ++i) {
        printf("%c",state->buffer_recv[i]);
    }

    http_serve_response(state);
    //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    return err;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb) {
    //TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    //printf("Client %i still connected\n", state->con_num);
    return ERR_OK;
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (err != ERR_ABRT) {
        printf("tcp_client_err_fn %d\n", err);
        //handle it TODO
        tcp_server_client_close(state);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {  //This is the only function that has a tcp_pcb callback arg
    if (err != ERR_OK || client_pcb == NULL) {
        printf("failure in accept\n");
        //handle it
        return ERR_VAL;
    }
    currently_connected++;
    printf("Client connected\n");

    //should we store them in a list? We get the callbacks anyway...
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));

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

    struct tcp_pcb* server_pcb = tcp_listen_with_backlog(pcb, 100);   //backlog, how many connections which are not yet accepted
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

    struct tcp_pcb *server_pcb = tcp_server_open();
    while(1){
        printf("alive... %i connected\n", currently_connected);
        sleep_ms(10000);

    }
    tcp_server_close(server_pcb);

    cyw43_arch_deinit();
    return 0;
}