# iotsaDoorbellButton

![build-platformio](https://github.com/cwi-dis/iotsaDoorbellButton/workflows/build-platformio/badge.svg)
![build-arduino](https://github.com/cwi-dis/iotsaDoorbellButton/workflows/build-arduino/badge.svg)

Server for a device with a push button and a keyswitch, for example used to indirectly control _iotsaDoorbellRinger_.

## Provisioning

Use the shared `iotsa` CLI (from the iotsa repo, `extras/python/`; see `iotsa-group/CLAUDE.md`). After a factory-fresh flash:

- **WiFi** — `iotsa networks` finds the `config-iotsa<suffix>` AP; then `iotsa --ssid config-iotsa<suffix> wifiConfig ssid=… ssidPassword=…`, then reboot.
- **Hostname** — `iotsa -t iotsa<suffix>.local --credentials owner:… configWait config hostName=…` (put the device in configuration mode when `configWait` asks; don't reboot it yourself to force the mode).
- **Capability issuer** — `iotsa -t <name>.local --credentials owner:… configWait xConfig capabilities trustedIssuer=<url> issuerKey=<shared-secret>`.
- **Button actions** — each button/keyswitch can be set (through the `buttons` module, gated by the device capability) to fire a GET at a preprogrammed URL, typically an _iotsaDoorbellRinger_'s `/api/alarm`.

