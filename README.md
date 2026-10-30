# titan_app
a collection of titan board and titan board mini projects

# Projects TOC
 Project name  | details |
| ------------- | ------------- |
|titan_mini_npu_ai_face_detection  | from titan mini built in example  |
| Titan_Board_Lorry_Capture | Titan Board: auto-captures a frame every 10s and uploads to a PC over Ethernet HTTP (used for debugging) |
| Titan_Board_Mini_Lorry_Capture | Titan Mini: same capture + upload, runs standalone (no terminal), LED toggles on each upload |

# Setup References
## Setting up titan board mini
refer to [Titan Board Mini 开发板](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/hw-board/ra8p1-titan-board-mini/ra8p1-titan-board-mini?id=titan-board-mini-%e5%bc%80%e5%8f%91%e6%9d%bf)

# system architecture
```mermaid
graph TD
    %% Styling
    classDef windows fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000;
    classDef titan fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000;
    classDef ubuntu fill:#fff3e0,stroke:#e65100,stroke-width:2px,color:#000;
    classDef protocol fill:#f3e5f5,stroke:#4a148c,stroke-dasharray: 5 5,color:#000;

    subgraph ASUS_Server ["ASUS Windows Server"]
        ADT_Playback["ADT Playback Software\n(Articulated Dump Truck Simulation / Video Loop)"]
        Display_Out["Display Output / HDMI Video Feed"]:::windows
    end

    subgraph Titan_Board ["RT-Thread Titan Board Mini"]
        Camera_Mod["Camera Module\n(Captures Playback Feed)"]
        MCU_Core["RT-Thread Real-Time OS\n(Edge Processing / Frame Capture)"]
        Titan_IO["UART / USB / Ethernet Interface"]
    end

    subgraph Ubuntu_Host ["A588 Single Board Computer (Ubuntu)"]
        App_Core["Vision Analytics / Processing Pipeline\n(OpenCV / Python / C++)"]
        Storage_Logs["Data Logging & Monitoring"]
    end

    %% Connections
    ADT_Playback -->|"Video Signal"| Display_Out
    Display_Out ==>|"Optical / HDMI / Capture Card"| Camera_Mod
    Camera_Mod --> MCU_Core
    MCU_Core --> Titan_IO
    
    Titan_IO ==>|"Serial (UART) / USB / TCP-IP over Ethernet"| App_Core
    App_Core --> Storage_Logs

    %% Apply Styles
    class ADT_Playback,Display_Out windows;
    class Camera_Mod,MCU_Core,Titan_IO titan;
    class App_Core,Storage_Logs ubuntu;
```
