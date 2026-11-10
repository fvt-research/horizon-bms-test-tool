# Architecture

## Layer Diagram

```mermaid
graph TD
    subgraph UI["UI Layer (ImGui / ImPlot)"]
        TX["TX Panel\n(ImGui)"]
        RX["RX Panel\n(ImGui)"]
        Plot["Plot Panel\n(ImPlot)"]
        CellViz["Cell Viz\n(ImDrawList)"]
    end

    subgraph DBC["DBC Model (dbcppp)"]
        DBCParser["DBC Parser\nmessages · signals · factor/offset/range"]
    end

    subgraph CAN["CAN Interface (abstract)"]
        direction LR
        PCAN["PCAN-Basic\n(Peak Hardware)"]
        Virtual["Virtual / Loopback\n(dev/test)"]
    end

    TX --> DBCParser
    RX --> DBCParser
    Plot --> DBCParser
    CellViz --> DBCParser
    DBCParser --> CAN
```

---

## Panels

| Panel | Library | Description |
|---|---|---|
| **Control Panel** | ImGui | BMU command TX controls with optional cyclic transmit, plus decoded BMS state display. |
| **Plot Panel** | ImPlot | User selects signals from a list; selected signals are rendered as scrolling time-series graphs with pan, zoom, and legend. |
| **Cell Temp Visualization** | ImGui Tables + ImDrawList primitives | Module-oriented tiles showing 2 thermistor values per module with summary cards. |
| **Cell Voltage Visualization** | ImGui Tables + ImDrawList primitives | Module-oriented tiles showing 12 cell voltages per module with summary cards. |
| **Cell Balancing Visualization** | ImGui Tables + ImDrawList primitives | Module-oriented tiles showing per-cell balancing current plus two indicators (blue command, green active). |

---

## Threading Model

```mermaid
sequenceDiagram
    participant HW as PCAN Hardware
    participant RX as CAN RX Thread
    participant Buf as RX Queue (mutex)
    participant UI as UI Thread (main)

    loop Every CAN frame
        HW->>RX: Raw CAN frame
        RX->>RX: Decode via dbcppp
        RX->>Buf: Push decoded signal values
    end

    loop Every render frame (~60 Hz)
        UI->>Buf: Drain pending values
        Buf-->>UI: Decoded signal values
        UI->>UI: Update plots & panels
    end
```

- The **CAN RX thread** runs independently of the UI, polling the CAN interface
  and sleeping briefly when no frame is available.
- Decoded frames are pushed into a **mutex-guarded queue**.
- The **UI thread** drains the queue at the start of each render frame.
- System command cyclic transmission runs on a separate worker, independent
  of the selected tab or render loop. The UI updates its encoded command and
  interval; disconnect stops the worker before closing the CAN interface.
  A failed transmission stops cyclic sending and is displayed in the menu bar.
- The optional `LuaScriptSession` prototype uses Lua and sol2 with local send
  and log callbacks. Its caller owns the thread, calls `runDue` for delayed
  actions and must serialize calls; callbacks must not reenter the session.
  The session has no timer thread and is not connected to the GUI or HIL.

---

## Module Visualization Design

Each visualization page uses module-grouped cards with spacing and headers.

```mermaid
graph LR
    Grid["Cell Grid\n(rows × cols = physical cells)"]
    Grid -->|color encodes value| Color["Heat-map color\n(green → yellow → red)"]
    Grid -->|hover| Popup["Per-cell popup\n(index, value, unit, status)"]
```

- Module header format: `Module XX (Cells A-B)`.
- Temperature page shows 2 thermistor tiles per module.
- Voltage and balancing pages show 12 cell tiles per module.
- Balancing tiles show current in the center and two top-right status lights.
