# UDP High-Speed Image Receiver & Display System

## 🚀 Project Overview
This project implements a **high-speed UDP image data receiver and display system**, capable of handling **24,000 packets per second**, each **1300 bytes**, to reconstruct and render real-time images. The system efficiently receives **raw image data over UDP**, processes it, and visualizes it using `QImage`. The software is optimized for **low-latency image rendering**, making it ideal for real-time applications like high-speed vision systems and industrial imaging.

### 🎥 Embedded System Need for the Software
- The Xilinx FPGA work as embedded system are required for this software, the dataflow pipeline are shows in graph below:
- ![image](https://github.com/user-attachments/assets/b0fc7950-9bdb-48c0-bc12-6f7179a92130)
- Support molecular migration and second creation
- High speed hardware accelerated SIMD structure for image data analysis and processing
- Visit another project page for the code: https://github.com/Sanssssssssssssssss/endoscopic-image-acquisition-system

## 🔥 Key Features
### 📡 High-Speed UDP Data Reception
- Supports receiving **24,000 UDP packets per second**.
- Each packet contains **1300 bytes**, optimized for **fast frame reconstruction**.
- Implements **buffer management & packet loss handling** for smooth visualization.

### 📺 Real-Time Image Display
- Uses **QImage & QPainter** for efficient real-time rendering.
- Supports **800x800 image resolution**, adaptable to other resolutions.
- **Frame interpolation** for missing data, ensuring display stability.

### 🎛 Advanced Image Processing & Control
- **Adjustable Parameters**:
  - ✅ **Brightness** Control
  - ✅ **Gamma Correction**
  - ✅ **Sharpness Adjustment**
  - ✅ **Noise Reduction**
- **Flip & Rotate Support**:
  - 🔄 **Horizontal and Vertical Flip**
- **Multi-Channel Measurement** (for future expansion):
  - Supports **multi-camera or multi-source image input processing**.

### 📁 Data Storage & Logging
- **📸 Snapshot Function**: Save current frames as `.png`.
- **🎥 Video Recording**: Supports **MP4 and AVI formats**.
- **📄 Logging System**: Records packet loss and transmission performance.

### 🛠 Advanced Debugging & Monitoring
- Integrated **tshark** support for monitoring UDP traffic.
- **Real-time FPS counter** to track system performance.

---

## 🏗 Software Architecture
The system consists of three main modules:

### **1️⃣ UDP Data Receiver**
- Listens for **UDP packets** on a specified port.
- Uses `QUdpSocket` for high-speed packet processing.
- Implements **buffer clearing mechanism** to prevent memory overflow.
- Supports **Tshark integration** for monitoring.

### **2️⃣ Image Processing & Display**
- Converts **raw UDP image data** into `QImage`.
- Handles **missing packet recovery and frame interpolation**.
- Implements **RGB565 to RGB888 conversion** for accurate color reproduction.
- Uses **QPainter for efficient display rendering**.

### **3️⃣ Control Panel UI**
- Provides **GUI controls** for adjusting:
  - Brightness, Gamma, Sharpness, Noise Reduction.
  - Flip settings (Horizontal & Vertical).
  - Snapshot & Video Recording.
- **Real-time FPS display** for monitoring performance.

---

## 📸 Software UI & Example
### **Live UDP Image Display & Control Panel**
![image](https://github.com/user-attachments/assets/1b500a7e-a768-4983-a153-92b2502a554b)


---

## 🔄 Future Improvements  
We plan to further optimize and expand the project:  

- **📈 Performance Enhancements**: Reduce processing latency to handle even higher frame rates.  
- **🖥️ Multi-Threaded Optimization**: Implement additional parallel processing for smoother rendering.  
- **🎨 UI Refinements**: Improve the interface for better usability.  
- **📡 Custom Protocol Support**: Extend compatibility with different UDP-based imaging systems.  
- **🛠 Hardware Acceleration**: Investigate GPU-based processing for real-time performance.  

### 🧠 Real-Time Machine Learning Detection  
- Integrate a **YOLO-based object detection model** to analyze and identify objects in real-time from the incoming video stream.  
- Implement **real-time anomaly detection** to flag unusual patterns in received images.  
- Use **TensorRT for optimized inference** on NVIDIA GPUs, achieving low-latency ML-based detection.  

### 📡 Edge AI Deployment  
- Explore **AI-assisted preprocessing** on **FPGA** or **Jetson platforms** to offload real-time processing tasks from the main CPU.  
- Optimize power-efficient edge computing solutions for high-speed image analysis.  

### 🎥 Advanced Video Encoding  
- Implement **H.265 hardware encoding** to efficiently store and transmit large image sequences with minimal bandwidth usage.  
- Improve video compression techniques for better data storage and transmission.  

These improvements will enhance **real-time processing capabilities**, making the system more efficient for **high-speed industrial inspection, medical imaging, and autonomous vision applications**. 🚀


## 🚀 Installation & Running
### **1️⃣ Install Dependencies**
Make sure you have all required libraries installed
