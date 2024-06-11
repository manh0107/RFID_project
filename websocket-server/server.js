const express = require('express');
const path = require('path');
const WebSocket = require('ws');
const mysql = require('mysql');
const os = require('os');

const app = express();
const port = 8080;

// Configure MySQL database
const db = mysql.createConnection({
  host: 'localhost',
  user: 'root',
  password: '',
  database: 'attendance_db'
});

db.connect(err => {
  if (err) {
    console.error('Failed to connect to MySQL database: ', err);
    process.exit(1);
  }
  console.log('Connected to MySQL database');
});

// Serve static files
app.use(express.static(path.join(__dirname, 'public')));

// Endpoint to get attendance records
app.get('/api/attendance_records', (req, res) => {
  const query = 'SELECT * FROM attendance_records';
  db.query(query, (err, results) => {
    if (err) {
      return res.status(500).send('Error fetching data');
    }
    res.json(results);
  });
});

// Start HTTP server
const server = app.listen(port, () => {
  console.log(`HTTP server running on port ${port}`);
  const networkInterfaces = os.networkInterfaces();
  Object.keys(networkInterfaces).forEach(interface => {
    networkInterfaces[interface].forEach(address => {
      if (address.family === 'IPv4' && !address.internal) {
        console.log(`Node.js server IP address: ${address.address}`);
      }
    });
  });
});

// Start WebSocket server
const wss = new WebSocket.Server({ server });

wss.on('connection', ws => {
  console.log('Client connected');

  ws.on('message', message => {
    console.log('Received: %s', message);
    const studentID = message;

    // Insert data into database
    const query = 'INSERT INTO attendance_records (student_id) VALUES (?)';
    db.query(query, [studentID], (err, results) => {
      if (err) {
        console.error('Error inserting data: ', err);
        ws.send('Error inserting data');
      } else {
        // Send confirmation message to client
        ws.send('Attendance recorded');

        // Broadcast message to all clients
        wss.clients.forEach(client => {
          if (client.readyState === WebSocket.OPEN) {
            client.send(studentID);
          }
        });
      }
    });
  });

  ws.on('close', () => {
    console.log('Client disconnected');
  });

  ws.on('error', error => {
    console.log('WebSocket error:', error);
  });
});

console.log('WebSocket server running on port 8080');
