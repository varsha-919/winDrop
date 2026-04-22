import React, { useEffect, useState } from "react";
import io from "socket.io-client";
import axios from "axios";

// const socket = io("http://localhost:5000");
// Change from "http://localhost:5000" to:
const socket = io(`http://${window.location.hostname}:5000`);

function App() {
  const [peers, setPeers] = useState([]);
  const [selectedFile, setSelectedFile] = useState(null);

  useEffect(() => {
    socket.on("peers_list", (peerList) => {
      setPeers(peerList);
    });

    return () => {
      socket.off("peers_list");
    };
  }, []);

  const handleSend = async (targetIp) => {
    if (!selectedFile) {
      alert("Select a file first!");
      return;
    }

    const formData = new FormData();
    formData.append("file", selectedFile);
    formData.append("targetIp", targetIp);

    try {
      // await axios.post("http://localhost:5000/send", formData);
      // Change from "http://localhost:5000/send" to:
await axios.post(`http://${window.location.hostname}:5000/send`, formData);
      alert("File sent!");
    } catch (err) {
      console.error(err);
      alert("Failed to send file");
    }
  };

  return (
    <div>
      <h1>🔦 Lighthouse</h1>

      <input type="file" onChange={(e) => setSelectedFile(e.target.files[0])} />

      <div>
        {peers.length === 0 && <p>🔍 Searching for devices...</p>}

        {peers.map((peer, i) => (
          <div key={peer.ip}>
            <h3>💻 {peer.name}</h3>
            <p>{peer.ip}</p>

            <button onClick={() => handleSend(peer.ip)}>Send File</button>
          </div>
        ))}
      </div>
    </div>
  );
}

export default App;
