import React, { useEffect, useState, useRef, useCallback } from "react";
import io from "socket.io-client";
import axios from "axios";
import "./App.css";

const socket = io(`http://${window.location.hostname}:5001`);

// DEBUG: Log socket connection status
socket.on("connect", () => {
  console.log("🔌 [SOCKET] Connected with ID:", socket.id);
});

socket.on("disconnect", () => {
  console.log("🔌 [SOCKET] Disconnected");
});

socket.on("connect_error", (err) => {
  console.error("🔌 [SOCKET] Connection error:", err.message);
});

function App() {
  const [peers, setPeers] = useState([]);
  const [selectedFile, setSelectedFile] = useState(null);
  const [isSearching, setIsSearching] = useState(true);
  const [sendingTo, setSendingTo] = useState(null);
  const [sendStatus, setSendStatus] = useState(null);
  const [isDragging, setIsDragging] = useState(false);
  const [dragCounter, setDragCounter] = useState(0);

  // 🔥 INCOMING REQUEST STATE
  const [incomingRequest, setIncomingRequest] = useState(null);
  const [isReceiving, setIsReceiving] = useState(false);
  const [receiveStatus, setReceiveStatus] = useState(null);

  // 🔥 TRANSFER PROGRESS STATE
  const [transferProgress, setTransferProgress] = useState(0);

  const fileInputRef = useRef(null);

  // 🔥 STABLE SOCKET LISTENERS - register once, never remove
  useEffect(() => {
    console.log("🔧 [EFFECT] Registering stable socket listeners");

    // Peers list
    socket.on("peers_list", (peerList) => {
      console.log("🔌 [SOCKET] peers_list received:", peerList.length, "peers");
      setPeers(peerList);
      setIsSearching(false);
    });

    // Incoming file request - trim requestId to remove carriage returns
    socket.on("incoming_request", (data) => {
      if (data.requestId) data.requestId = data.requestId.trim();
      console.log("🔌 [SOCKET] incoming_request received:", data);
      setIncomingRequest(data);
      setReceiveStatus("pending");
    });

    // Transfer progress
    socket.on("transfer_progress", (data) => {
      console.log("🔌 [SOCKET] transfer_progress received:", data);
      // Keep sendingTo set until final state is shown
      setTransferProgress(data.progress);
    });

    // Request rejected - trim requestId
    socket.on("request_rejected", (data) => {
      if (data.requestId) data.requestId = data.requestId.trim();
      console.log("🔌 [SOCKET] request_rejected received:", data);
      setSendStatus({ success: false, rejected: true, reason: data.reason });
      setSendingTo(null);
      setTransferProgress(0);
    });

    // Transfer delivered (sender side) - trim requestId, keep sendingTo to show success state
    socket.on("transfer_delivered", (data) => {
      if (data.requestId) data.requestId = data.requestId.trim();
      console.log("🔌 [SOCKET] transfer_delivered received:", data);
      setSendStatus({ success: true, delivered: true });
      setSendingTo(null);
      setTransferProgress(100);
    });

    // Transfer complete (receiver side) - trim requestId
    socket.on("transfer_complete", (data) => {
      if (data.requestId) data.requestId = data.requestId.trim();
      console.log("🔌 [SOCKET] transfer_complete received:", data);
      setReceiveStatus("complete");
      setIsReceiving(false);
    });

    return () => {
      console.log("🔧 [EFFECT] Cleaning up stable socket listeners");
      socket.off("peers_list");
      socket.off("incoming_request");
      socket.off("transfer_progress");
      socket.off("request_rejected");
      socket.off("transfer_delivered");
      socket.off("transfer_complete");
    };
  }, []); // Empty deps - register once, never re-register

  // 🔥 REGISTER CLIENT IP - separate effect that CAN re-run
  useEffect(() => {
    const registerClient = async () => {
      try {
        const res = await axios.get(
          `http://${window.location.hostname}:5001/my-ip`,
        );

        socket.emit("register", {
          ip: res.data.ip,
        });

        console.log("📱 Registered with IP:", res.data.ip);
      } catch (err) {
        console.error("Registration failed:", err);
      }
    };

    registerClient();
  }, []);

  // 🔥 START TRANSFER (called after accept)
  // OLD IMPLEMENTATION - COMMENTED OUT: This function calls /start-transfer which
  // spawns a second sender.cpp process. In the new TCP workflow, sender.cpp stays
  // alive and automatically continues the transfer after receiving ACCEPT via TCP.
  // No second sender process should be spawned.
  /*
  const handleStartTransfer = async (requestId, targetIp) => {
    try {
      await axios.post(
        `http://${window.location.hostname}:5001/start-transfer`,
        { requestId, targetIp },
      );
      // Status will be updated via socket events
    } catch (err) {
      console.error("Failed to start transfer:", err);
      setSendStatus({ success: false, ip: sendingTo });
      setSendingTo(null);
    }
  };
  */

  const handleDragEnter = useCallback((e) => {
    e.preventDefault();
    e.stopPropagation();
    setDragCounter((prev) => prev + 1);
    setIsDragging(true);
  }, []);

  const handleDragLeave = useCallback((e) => {
    e.preventDefault();
    e.stopPropagation();
    setDragCounter((prev) => {
      const newCount = prev - 1;
      if (newCount === 0) {
        setIsDragging(false);
      }
      return newCount;
    });
  }, []);

  const handleDragOver = useCallback((e) => {
    e.preventDefault();
    e.stopPropagation();
  }, []);

  const handleDrop = useCallback((e) => {
    e.preventDefault();
    e.stopPropagation();
    setIsDragging(false);
    setDragCounter(0);

    const files = e.dataTransfer.files;
    if (files && files.length > 0) {
      setSelectedFile(files[0]);
    }
  }, []);

  const handleSend = async (targetIp) => {
    if (!selectedFile) return;
    setSendingTo(targetIp);
    setSendStatus(null);
    setTransferProgress(0);

    // Step 1: Upload file first (with requestMode=true)
    const formData = new FormData();
    formData.append("file", selectedFile);
    formData.append("targetIp", targetIp);
    formData.append("requestMode", "true");

    try {
      await axios.post(
        `http://${window.location.hostname}:5001/send`,
        formData,
      );
      // File uploaded, now send TCP-based request
    } catch (err) {
      console.error("File upload failed:", err);
      setSendStatus({ success: false, ip: targetIp });
      setSendingTo(null);
      return;
    }

    // Step 2: Send TCP-based transfer request via HTTP endpoint
    // This will spawn sender.cpp in request mode, which communicates via TCP
    setSendStatus({ waiting: true, ip: targetIp });

    try {
      const response = await axios.post(
        `http://${window.location.hostname}:5001/send-request`,
        {
          targetIp,
          filename: selectedFile.name,
          fileSize: selectedFile.size,
          fileType: selectedFile.type,
        },
      );

      const { requestId, status } = response.data;

      if (status === "accepted") {
        setSendStatus({ success: true, ip: targetIp });
        setSendingTo(null);
      } else if (status === "rejected") {
        setSendStatus({
          success: false,
          rejected: true,
          reason: response.data.reason,
          ip: targetIp,
        });
        setSendingTo(null);
      } else if (status === "failed") {
        setSendStatus({ success: false, ip: targetIp });
        setSendingTo(null);
      }
      // If status is "pending" or "waiting", wait for socket events
    } catch (err) {
      console.error("Transfer request failed:", err);
      setSendStatus({ success: false, ip: targetIp });
      setSendingTo(null);
    }
  };

  // 🔥 HANDLE ACCEPT
  const handleAccept = async () => {
    if (!incomingRequest) return;

    const { requestId } = incomingRequest;

    // Send ACCEPT to backend via HTTP (writes to response file for core.cpp)
    try {
      await axios.post(
        `http://${window.location.hostname}:5001/respond-request`,
        { requestId, action: "ACCEPT" },
      );
    } catch (err) {
      console.error("Failed to accept:", err);
    }

    setReceiveStatus("accepted");
    setIncomingRequest(null);
    setIsReceiving(true);
  };

  // 🔥 HANDLE REJECT
  const handleReject = async () => {
    if (!incomingRequest) return;

    const { requestId } = incomingRequest;

    // Send REJECT to backend via HTTP (writes to response file for core.cpp)
    try {
      await axios.post(
        `http://${window.location.hostname}:5001/respond-request`,
        { requestId, action: "REJECT" },
      );
    } catch (err) {
      console.error("Failed to reject:", err);
    }

    setIncomingRequest(null);
    setReceiveStatus(null);
    setReceiveStatus(null);
  };

  const openFileDialog = () => {
    fileInputRef.current?.click();
  };

  const getDropContent = () => {
    if (isDragging) {
      return <span className="drop-text-highlight">Drop to send</span>;
    }
    if (selectedFile) {
      return (
        <div className="file-info">
          <span className="file-name">{selectedFile.name}</span>
          <span className="file-size">
            {(selectedFile.size / 1024 / 1024).toFixed(2)} MB
          </span>
        </div>
      );
    }
    return "Drop your file here";
  };

  const getDropIconClass = () => {
    let className = "drop-icon";
    if (isDragging) className += " dragging";
    if (selectedFile) className += " selected";
    return className;
  };

  const getDropIconChar = () => {
    if (isDragging) return "↓";
    if (selectedFile) return "✓";
    return "+";
  };

  // 🔥 FORMAT FILE SIZE
  const formatFileSize = (bytes) => {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    if (bytes < 1024 * 1024 * 1024)
      return (bytes / 1024 / 1024).toFixed(2) + " MB";
    return (bytes / 1024 / 1024 / 1024).toFixed(2) + " GB";
  };

  // 🔥 GET FILE ICON
  const getFileIcon = (filename) => {
    const ext = filename.split(".").pop()?.toLowerCase();
    const iconMap = {
      pdf: "📄",
      doc: "📝",
      docx: "📝",
      xls: "📊",
      xlsx: "📊",
      ppt: "📽️",
      pptx: "📽️",
      txt: "📃",
      zip: "📦",
      rar: "📦",
      "7z": "📦",
      jpg: "🖼️",
      jpeg: "🖼️",
      png: "🖼️",
      gif: "🖼️",
      webp: "🖼️",
      svg: "🖼️",
      mp3: "🎵",
      wav: "🎵",
      flac: "🎵",
      aac: "🎵",
      mp4: "🎬",
      mkv: "🎬",
      avi: "🎬",
      mov: "🎬",
      webm: "🎬",
      exe: "⚙️",
      msi: "⚙️",
      dmg: "💿",
      deb: "📦",
      rpm: "📦",
      js: "💻",
      ts: "💻",
      py: "💻",
      java: "💻",
      cpp: "💻",
      c: "💻",
      html: "🌐",
      css: "🎨",
      json: "📋",
      xml: "📋",
    };
    return iconMap[ext] || "📁";
  };

  // 🔥 GET BUTTON TEXT BASED ON STATUS
  const getButtonText = (peer) => {
    const isSending = sendingTo === peer.ip;
    const isDone = sendStatus?.ip === peer.ip;
    const success = isDone && sendStatus?.success;
    const failed = isDone && !sendStatus?.success;
    const waiting = isDone && sendStatus?.waiting;

    if (success && sendStatus?.delivered) return "✓ Delivered";
    if (success) return "✓ Sent";
    if (failed) return "✗ Failed";
    if (sendStatus?.rejected) return "✗ Rejected";
    if (waiting) return "⏳ Waiting...";
    if (isSending) return "⏳ Sending...";
    return "Send";
  };

  return (
    <div className="container">
      <div className="card">
        <div className="header">
          <div className="logo-wrapper">
            <div className="logo">⚡</div>
          </div>
          <h1 className="title">WinDrop</h1>
          <p className="subtitle">Fast file transfers across your network</p>
        </div>

        <div
          className={`drop-zone ${isDragging ? "dragging" : ""}`}
          onDragEnter={handleDragEnter}
          onDragLeave={handleDragLeave}
          onDragOver={handleDragOver}
          onDrop={handleDrop}
          onClick={openFileDialog}
        >
          <input
            ref={fileInputRef}
            type="file"
            onChange={(e) =>
              e.target.files[0] && setSelectedFile(e.target.files[0])
            }
            style={{ display: "none" }}
          />
          <div className={getDropIconClass()}>{getDropIconChar()}</div>
          <div className="drop-text">{getDropContent()}</div>
        </div>

        <div className="status-bar">
          {isSearching && (
            <div className="searching">
              <div className="search-ring">
                <div className="search-dot" />
              </div>
              <span>Scanning network...</span>
            </div>
          )}
          {!isSearching && peers.length > 0 && (
            <div className="device-count">
              <span className="count">{peers.length}</span>
              <span>device{peers.length !== 1 ? "s" : ""} online</span>
            </div>
          )}
          {!isSearching && peers.length === 0 && (
            <div className="no-devices">No devices on network</div>
          )}
        </div>

        <div className="devices-list">
          {peers.map((peer) => {
            const isSending = sendingTo === peer.ip;
            const isDone = sendStatus?.ip === peer.ip;
            const success = isDone && sendStatus?.success;
            const failed = isDone && !sendStatus?.success;
            const waiting = isDone && sendStatus?.waiting;

            // Show progress when actively sending OR when transfer just completed (success/failed)
            const showProgress = (isSending || (isDone && transferProgress > 0));

            let cardClass = "device-card";
            if (success && sendStatus?.delivered) cardClass += " success";
            if (success && !sendStatus?.delivered) cardClass += " transferring";
            if (failed || sendStatus?.rejected) cardClass += " error";

            let buttonClass = "send-button";
            if (success && sendStatus?.delivered) buttonClass += " success";
            if (success && !sendStatus?.delivered)
              buttonClass += " transferring";
            if (failed || sendStatus?.rejected) buttonClass += " error";
            if (isSending || waiting) buttonClass += " sending";
            if (!selectedFile) buttonClass += " disabled";

            return (
              <div key={peer.ip} className={cardClass}>
                <div className="device-left">
                  <div className="device-icon">📱</div>
                  <div className="device-info">
                    <div className="device-name">{peer.name}</div>
                    <div className="device-ip">{peer.ip}</div>
                    {showProgress && (
                      <div className="transfer-progress">
                        <div className="progress-bar">
                          <div
                            className="progress-fill"
                            style={{ width: `${transferProgress}%` }}
                          ></div>
                        </div>
                        <span className="progress-text">
                          {transferProgress}%
                        </span>
                      </div>
                    )}
                  </div>
                </div>
                <button
                  className={buttonClass}
                  onClick={() => handleSend(peer.ip)}
                  disabled={
                    !selectedFile ||
                    isSending ||
                    waiting ||
                    (isDone && !failed && !sendStatus?.rejected)
                  }
                >
                  {isSending ? (
                    <Spinner />
                  ) : success ? (
                    <AnimatedCheck />
                  ) : failed || sendStatus?.rejected ? (
                    <FailedX />
                  ) : (
                    getButtonText(peer)
                  )}
                </button>
              </div>
            );
          })}
        </div>

        {/* 🔥 INCOMING REQUEST MODAL */}
        {incomingRequest && (
          <div className="modal-overlay">
            <div className="modal-content">
              <div className="modal-header">
                <h2>Incoming File</h2>
              </div>
              <div className="modal-body">
                <div className="modal-file-icon">
                  {getFileIcon(incomingRequest.filename)}
                </div>
                <div className="modal-file-info">
                  <div className="modal-filename">
                    {incomingRequest.filename}
                  </div>
                  <div className="modal-filesize">
                    {formatFileSize(incomingRequest.fileSize)}
                  </div>
                  <div className="modal-sender">
                    From: {incomingRequest.senderName}
                  </div>
                </div>
              </div>
              <div className="modal-actions">
                <button className="modal-reject-btn" onClick={handleReject}>
                  Reject
                </button>
                <button className="modal-accept-btn" onClick={handleAccept}>
                  Accept
                </button>
              </div>
            </div>
          </div>
        )}

        {/* 🔥 RECEIVE STATUS */}
        {isReceiving && receiveStatus && (
          <div className="receive-status">
            {receiveStatus === "accepted" && "Accepted - Receiving file..."}
            {receiveStatus === "complete" && "File received successfully!"}
          </div>
        )}
      </div>
    </div>
  );
}

function Spinner() {
  return (
    <svg width="18" height="18" viewBox="0 0 24 24" className="spinner">
      <circle
        cx="12"
        cy="12"
        r="10"
        stroke="white"
        strokeWidth="2.5"
        fill="none"
        opacity="0.3"
      />
      <path
        d="M12 2 A10 10 0 0 1 22 12"
        stroke="white"
        strokeWidth="2.5"
        fill="none"
        strokeLinecap="round"
      />
    </svg>
  );
}

function AnimatedCheck() {
  return (
    <svg width="18" height="18" viewBox="0 0 24 24">
      <circle
        className="windrop-circle"
        cx="12"
        cy="12"
        r="10"
        fill="#22c55e"
      />
      <path
        className="windrop-path"
        d="M7 12.5l3.5 3.5 6.5-7"
        stroke="white"
        strokeWidth="2.5"
        strokeLinecap="round"
        strokeLinejoin="round"
        fill="none"
      />
    </svg>
  );
}

function FailedX() {
  return (
    <svg width="18" height="18" viewBox="0 0 24 24">
      <circle cx="12" cy="12" r="10" fill="#f43f5e" className="failed-circle" />
      <path
        d="M8 8l8 8M16 8l-8 8"
        stroke="white"
        strokeWidth="2.5"
        strokeLinecap="round"
      />
    </svg>
  );
}

export default App;
