#pragma once
// Packet storage/cookies and list operations are explicit KPI boundaries. The
// node retirement append/drain code itself is extracted unchanged from source.
struct Packet {
    Packet *next = nullptr;
    void *cookie = nullptr;
};
using mbuf_t = Packet *;
using ifnet_t = void *;
struct mbuf_list { Packet *head = nullptr, *tail = nullptr; };
#define MBUF_LIST_INITIALIZER() {}
static void mbuf_pkthdr_setrcvif(Packet *packet, void *cookie) { packet->cookie = cookie; }
static void *mbuf_pkthdr_rcvif(Packet *packet) { return packet->cookie; }
static void ml_enqueue(mbuf_list *list, Packet *packet) {
    assert(packet->next == nullptr);
    if (list->tail) list->tail->next = packet;
    else list->head = packet;
    list->tail = packet;
}
static Packet *ml_dequeue(mbuf_list *list) {
    Packet *packet = list->head;
    if (packet) {
        list->head = packet->next;
        packet->next = nullptr;
        if (!list->head) list->tail = nullptr;
    }
    return packet;
}
