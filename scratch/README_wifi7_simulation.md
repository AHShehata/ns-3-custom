
# Wi-Fi 7 MLO Simulation - ns-3.41

This document provides a comprehensive explanation of the usage and configuration of the simulation script named `wifi7_DL_legacy_2OBSS_corr`, developed for the **ns-3.41** network simulator. The script is designed to assess the performance of **Wi-Fi 7 Multi-Link Operation (MLO)** in a scenario where a target Access Point (AP) is contended by two Overlapping Basic Service Sets (OBSS). It supports two operational modes: **STR (Simultaneous Transmit and Receive)** and **EMLSR (Enhanced Multi-Link Single Radio)**.

The simulation environment is highly flexible, allowing the user to configure various parameters related to antenna setups, PHY/MAC layers, interference conditions, and OBSS behavior.

## Example Command

To launch a typical simulation, here is the following command as a reference:

```bash
./ns3 run "wifi7_DL_legacy_2OBSS_corr --frequency=2.4 --frequency2=0 --frequency3=0 --emlsrLinks=0 --antennas_AP=1 --antennas_Sta=2 --EMLSR_mode=false --Links_number=1 --Number_runs=20"
```

This command executes a downlink UDP transmission over a single link operating at 2.4 GHz, with 1 antenna at the AP and 2 antennas at the station, and with STR mode enabled (`EMLSR_mode=false`).

## Configurable Parameters

All configurable parameters are defined at the beginning of the script. These include:

- `simulationTime`: Duration of the simulation
- `frequency`, `frequency2`, `frequency3`: Frequency bands for the first, second, and third links
- `mcs`: Modulation and coding scheme
- `payloadSize`: Size of transmitted packets

### Operational Mode

To switch between STR and EMLSR modes:

- Set `EMLSR_mode=true` to enable EMLSR
- Use `emlsrLinks` (comma-separated list of link IDs) to define active links
- Set the total number of active links via `Links_number`

## OBSS Configuration

To simulate OBSS interference:

- Adjust traffic load using `traffic_load_BBS1`, `traffic_load_BBS2`, etc.
- Enable OBSS PD using `enableObssPd`
- Set detection threshold using `obssPdThreshold`

## Antenna & MAC/PHY Settings

- Set antenna counts using `antennas_AP`, `antennas_Sta`, `antennas_OBSS`
- Enable/disable RTS/CTS using `useRts`
- Enable/disable extended block acknowledgment via `useExtendedBlockAck`

## Channel Model

- Change the error model to simulate different channel types (AWGN, Channel B, Channel D)

## Stations and Output

- The number of stations per AP (target or OBSS) is also configurable.
- Output includes throughput and latency per run.
- Per-packet latency can be enabled for CDF analysis of delays.

## Additional Simulation Variants

- `wifi7_DL_legacy_3OBSS_corr`: Simulates contention from **three OBSS APs**
- `wifi7_DL_OBSS3_GEN`: Models coexistence with **Wi-Fi 7-capable OBSS APs**
- `wifi7_DL`: Runs **Wi-Fi 7 simulation without any OBSS networks**, used as a baseline.

