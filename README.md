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
flowchart LR
    %% Styling
    classDef windows fill:#e1f5fe,stroke:#01579b,stroke-width:2px,color:#000;
    classDef titan fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000;
    classDef ubuntu fill:#fff3e0,stroke:#e65100,stroke-width:2px,color:#000;
    classDef web fill:#f3e5f5,stroke:#4a148c,stroke-width:2px,color:#000;

    subgraph ASUS_Server ["ASUS Windows Server"]
        direction TB
        ADT_Playback["ADT Playback<br/>(Articulated Dump Truck<br/>Video Loop)"]
        Monitor["Monitor Display"]
        ADT_Playback -->|"Video Signal"| Monitor
    end

    subgraph Titan_Board ["RT-Thread Titan Board Mini"]
        direction TB
        Camera_Mod["OV5640 Camera Module<br/>(MIPI CSI, 640x480)"]
        Capture_Loop["RT-Thread Capture Loop<br/>(1 frame every 10 s)"]
        Titan_ETH["HTTP Client<br/>over Ethernet"]
        Camera_Mod --> Capture_Loop --> Titan_ETH
    end

    subgraph Right_Column [" "]
        direction TB

        subgraph Ubuntu_Host ["A588 (Ubuntu)"]
            direction TB
            Receiver["HTTP Receiver<br/>(receive_face_http.py, port 5000)"]
            Converter["Frame Decoder<br/>(RGB565 to PNG)"]
            Storage["Image Dataset<br/>(captures_http/)"]
            Receiver --> Converter --> Storage
        end

        subgraph Web_Layer ["Web App"]
            WebApp["Web Application"]
        end

        Ubuntu_Host ==>|"Images / Results"| Web_Layer
    end

    ASUS_Server ==>|"Optical Capture<br/>(camera points at screen)"| Titan_Board
    Titan_Board ==>|"TCP/IP over Ethernet<br/>(HTTP POST /upload)"| Right_Column

    %% Apply Styles
    class ADT_Playback,Monitor windows;
    class Camera_Mod,Capture_Loop,Titan_ETH titan;
    class Receiver,Converter,Storage ubuntu;
    class WebApp web;
    style Right_Column fill:none,stroke:none
```
photo below shows the setup used for this project
![setup of hardware](Titan_Mini_Lorry_Capture/figures/image-20251029173335454.png)
