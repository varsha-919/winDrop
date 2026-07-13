import React, { useEffect, useState, useRef, useCallback } from "react";
import io from "socket.io-client";
import axios from "axios";
import "./App.css";

const socket = io(`http://${window.location.hostname}:5000`);

function App() {
  const [peers, setPeers] = useState([]);
  const [selectedFile, setSelectedFile] = useState(null);
  const [isSearching, setIsSearching] = useState(true);
  const [sendingTo, setSendingTo] = useState(null);
  const [sendStatus, setSendStatus] = useState(null);
  const [isDragging, setIsDragging] = useState(false);
  const [dragCounter, setDragCounter] = useState(0);
  const fileInputRef = useRef(null);

  useEffect(() => {
    socket.on("peers_list", (peerList) => {
      setPeers(peerList);
      setIsSearching(false);
    });

    return () => {
      socket.off("peers_list");
    };
  }, []);

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

    const formData = new FormData();
    formData.append("file", selectedFile);
    formData.append("targetIp", targetIp);

    try {
      await axios.post(
        `http://${window.location.hostname}:5000/send`,
        formData,
      );
      setSendStatus({ success: true, ip: targetIp });
    } catch (err) {
      console.error(err);
      setSendStatus({ success: false, ip: targetIp });
    }
    setSendingTo(null);
  };

  const openFileDialog = () => {
    fileInputRef.current?.click();
  };

  const getDropContent = () => {
    if (isDragging) {
      return (
        <span className="drop-text-highlight">Drop to send</span>
      );
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
          <div className={getDropIconClass()}>
            {getDropIconChar()}
          </div>
          <div className="drop-text">
            {getDropContent()}
          </div>
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

            let cardClass = "device-card";
            if (success) cardClass += " success";
            if (failed) cardClass += " error";

            let buttonClass = "send-button";
            if (success) buttonClass += " success";
            if (failed) buttonClass += " error";
            if (isSending) buttonClass += " sending";
            if (!selectedFile) buttonClass += " disabled";

            return (
              <div key={peer.ip} className={cardClass}>
                <div className="device-left">
                  <div className="device-icon">📱</div>
                  <div className="device-info">
                    <div className="device-name">{peer.name}</div>
                    <div className="device-ip">{peer.ip}</div>
                  </div>
                </div>
                <button
                  className={buttonClass}
                  onClick={() => handleSend(peer.ip)}
                  disabled={!selectedFile || isSending}
                >
                  {isSending ? (
                    <Spinner />
                  ) : success ? (
                    <AnimatedCheck />
                  ) : failed ? (
                    <FailedX />
                  ) : (
                    "Send"
                  )}
                </button>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}

function Spinner() {
  return (
    <svg
      width="18"
      height="18"
      viewBox="0 0 24 24"
      className="spinner"
    >
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
      <circle
        cx="12"
        cy="12"
        r="10"
        fill="#f43f5e"
        className="failed-circle"
      />
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