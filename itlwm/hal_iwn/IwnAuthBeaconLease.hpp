#ifndef IWN_AUTH_BEACON_LEASE_HPP
#define IWN_AUTH_BEACON_LEASE_HPP

#include <stdint.h>
#include <HAL/ItlStateTransitionLease.hpp>

/* Host-only value ownership. The HAL serializes this record; no node, mbuf,
 * credential or raw IE survives the ingress callback in this lease. */
struct IwnAuthBeaconRequest {
    uint64_t serial;
    uint64_t hardwareGeneration;
    ItlStateTransitionIdentity identity;
    uint64_t reassocSerial;
    uint64_t reassocSequence;
    uint64_t deadline;
    int sourceState;
    int state;
    int argument;
    uint16_t channel;
    uint8_t bssid[6];
    uint8_t station[6];
    bool requireBeacon;
};

struct IwnAuthBeaconLease {
    enum class Stage : uint8_t { Empty, Queued, Programming, Waiting, Claimed };
    uint64_t nextSerial;
    uint64_t cancellationSequence;
    uint64_t hardwareGeneration;
    bool open;
    IwnAuthBeaconRequest request;
    Stage stage;
    bool rxonSubmitted;
    bool rxonAccepted;
    bool beaconSeen;
    uint16_t rxonIndex;
    uint8_t preparationSubmitted;
    uint8_t preparationAccepted;
    uint16_t preparationIndex[2];
    int error;

    bool pending() const {
        return stage == Stage::Queued || stage == Stage::Programming ||
            stage == Stage::Waiting;
    }
    void cancel() {
        if (cancellationSequence != UINT64_MAX)
            ++cancellationSequence;
        request = IwnAuthBeaconRequest{};
        stage = Stage::Empty;
        rxonSubmitted = rxonAccepted = beaconSeen = false;
        rxonIndex = 0;
        preparationSubmitted = preparationAccepted = 0;
        preparationIndex[0] = preparationIndex[1] = 0;
        error = 0;
    }
    void close() {
        open = false;
        cancel();
    }
    bool reopen() {
        close();
        if (hardwareGeneration == UINT64_MAX)
            return false;
        ++hardwareGeneration;
        open = true;
        return true;
    }
    bool current(const IwnAuthBeaconRequest &copy) const {
        return open && copy.serial != 0 && copy.serial == request.serial &&
            copy.hardwareGeneration == hardwareGeneration &&
            request.hardwareGeneration == hardwareGeneration;
    }
    bool enqueue(const IwnAuthBeaconRequest &input, IwnAuthBeaconRequest *copy) {
        if (!open || copy == nullptr || nextSerial == UINT64_MAX ||
            input.identity.associationEpoch == 0 || input.channel == 0 ||
            input.deadline == 0)
            return false;
        // Repeated ingress for the same operation must not restart its
        // deadline or RXON transaction. A new association epoch is distinct.
        bool same = pending() && input.identity.equals(request.identity) &&
            input.reassocSerial == request.reassocSerial &&
            input.reassocSequence == request.reassocSequence &&
            input.sourceState == request.sourceState && input.state == request.state &&
            input.argument == request.argument && input.channel == request.channel;
        for (unsigned i = 0; i != 6; ++i)
            same = same && input.bssid[i] == request.bssid[i] &&
                input.station[i] == request.station[i];
        if (same) {
            *copy = request;
            return true;
        }
        cancel();
        request = input;
        request.serial = ++nextSerial;
        request.hardwareGeneration = hardwareGeneration;
        stage = Stage::Queued;
        *copy = request;
        return true;
    }
    bool takeProgramming(uint64_t now, int timeoutError, IwnAuthBeaconRequest *copy) {
        if (!open || copy == nullptr || stage != Stage::Queued)
            return false;
        if (now >= request.deadline) {
            error = timeoutError;
            stage = Stage::Waiting;
            return false;
        }
        *copy = request;
        stage = Stage::Programming;
        return true;
    }
    bool submitRxon(const IwnAuthBeaconRequest &copy, uint16_t index) {
        if (!current(copy) || stage != Stage::Programming || rxonSubmitted)
            return false;
        rxonSubmitted = true;
        rxonIndex = index;
        return true;
    }
    bool noteRxon(uint64_t serial, uint16_t index, bool accepted, int failure, uint64_t now) {
        if (!open || serial == 0 || serial != request.serial ||
            !pending() || !rxonSubmitted || index != rxonIndex ||
            rxonAccepted || error != 0 || now >= request.deadline)
            return false;
        if (accepted)
            rxonAccepted = true;
        else
            error = failure;
        return true;
    }
    bool submitPreparation(const IwnAuthBeaconRequest &copy, unsigned slot, uint16_t index) {
        if (!current(copy) || stage != Stage::Programming || !rxonSubmitted ||
            slot >= 2 || (preparationSubmitted & (1U << slot)) != 0)
            return false;
        preparationSubmitted |= 1U << slot;
        preparationIndex[slot] = index;
        return true;
    }
    bool notePreparation(uint64_t serial, unsigned slot, uint16_t index,
                         bool accepted, int failure, uint64_t now) {
        if (!open || serial == 0 || serial != request.serial || !pending() ||
            slot >= 2 || (preparationSubmitted & (1U << slot)) == 0 ||
            (preparationAccepted & (1U << slot)) != 0 ||
            preparationIndex[slot] != index || error != 0 || now >= request.deadline)
            return false;
        if (accepted)
            preparationAccepted |= 1U << slot;
        else
            error = failure;
        return true;
    }
    bool noteBeacon(const uint8_t *bssid, const uint8_t *sender, uint16_t channel, uint64_t now) {
        if (!open || !pending() || !request.requireBeacon || !rxonAccepted ||
            error != 0 || beaconSeen || channel != request.channel ||
            bssid == nullptr || sender == nullptr || now >= request.deadline)
            return false;
        for (unsigned i = 0; i != 6; ++i)
            if (bssid[i] != request.bssid[i] || sender[i] != request.bssid[i])
                return false;
        beaconSeen = true;
        return true;
    }
    bool programmed(const IwnAuthBeaconRequest &copy, int result) {
        if (!current(copy) || stage != Stage::Programming)
            return false;
        if (result != 0)
            error = result;
        stage = Stage::Waiting;
        return true;
    }
    bool takeTerminal(uint64_t now, int timeoutError,
                      IwnAuthBeaconRequest *copy, int *result) {
        if (!open || copy == nullptr || result == nullptr ||
            stage != Stage::Waiting)
            return false;
        if (error == 0 && (!rxonAccepted || preparationAccepted != 3 ||
            (request.requireBeacon && !beaconSeen))) {
            if (now < request.deadline)
                return false;
            error = timeoutError;
        }
        *copy = request;
        *result = error;
        stage = Stage::Claimed;
        return true;
    }
};

#endif
