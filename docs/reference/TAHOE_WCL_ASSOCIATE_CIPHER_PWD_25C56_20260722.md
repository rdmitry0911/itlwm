# Tahoe 25C56 WCL associate and CIPHER_PWD carrier

Date: 2026-07-22

## Scope and provenance

This read-only recovery note is pinned to macOS 26.2 / 25C56 BootKC
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
It records only the private WCL candidate boundary needed to keep a normal
saved-profile SAE password separate from public key APIs. It neither reads a
System Keychain value nor claims an on-air WPA3 join.

## Actual association route

`Apple80211Associate2` builds the public association request. The WCL request
copies its embedded `apple80211_key`, and `WCLJoinManager` sends the final
candidate as IOC `0x1ba`, length `0x6fc`. The
`apple80211setWCL_ASSOCIATE` dispatcher invokes the direct
`setWCL_ASSOCIATE` virtual. This is the only recovered local ingress that may
expose the pre-association key to a driver.

Relevant fields in that `0x6fc` candidate are:

- AP mode `+0x0c`; auth lower/upper/flags `+0x10/+0x14/+0x18`;
- SSID length/data `+0x1c/+0x20`;
- `apple80211_key` at `+0x40`: length `+0x44`, cipher `+0x48`, password
  window `+0x50` (64 bytes);
- RSN IE length/data `+0xd4/+0xd6`;
- candidate count/first BSSID `+0x218/+0x220`.

For `APPLE80211_CIPHER_PWD = 10`, JoinAdapter caps the password input window
at 64 bytes. A SAE consumer must require an explicit length 8..63, copy only
that many bytes, and never reinterpret `CIPHER_KEY` as this producer.

## Negative boundary

The `0x3ad8` carrier is not association data in 25C56. Its matching routes
belong to WOW parameters and use selectors `0x45/0x46`. It has no demonstrated
conversion to the `0x1ba/0x6fc` candidate and must never be parsed as an
`apple80211_key` or a CIPHER_PWD source.

This supersedes the older hidden-association interpretation for current
25C56.

