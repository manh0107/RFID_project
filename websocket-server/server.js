const express = require('express');
const path = require('path');
const WebSocket = require('ws');
const mysql = require('mysql');
const os = require('os');

const app = express();
const port = 8080;

const db = mysql.createConnection({
  host: 'localhost',
  user: 'root',
  password: '',
  database: 'attendance_db'
});

db.connect(err => {
  if (err) {
    console.error('Cannot connect to MySQL: ', err);
    process.exit(1);
  }
  console.log('Connected to MySQL');
});

app.use(express.static(path.join(__dirname, 'public')));

app.get('/api/attendance_records', (req, res) => {
  const query = 'SELECT * FROM attendance_records';
  db.query(query, (err, results) => {
    if (err) {
      console.error('Failed to retrieve data: ', err);
      res.status(500).send('Failed to retrieve data');
    } else {
      res.json(results);
    }
  });
});

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

const wss = new WebSocket.Server({ server });

wss.on('connection', ws => {
  console.log('Client connected');

  ws.on('message', message => {
    const messageStr = message.toString();
    console.log('Received: %s', messageStr);
    const parts = messageStr.split(";");

    if (parts.length !== 3) {
      console.log(`Invalid message format: ${messageStr}`);
      ws.send(`Invalid message format`);
      return;
    }

    const [studentID, scanType, formattedTime] = parts;
    const [datePart, timePart] = formattedTime.split(" ");

    if (!datePart || !timePart) {
      console.log(`Invalid date/time format: ${formattedTime}`);
      ws.send(`Invalid date/time format`);
      return;
    }

    const [day, month, year] = datePart.split("-");
    const dateAttend = `${year}-${month}-${day}`; // Định dạng YYYY-MM-DD
    const timeAttend = timePart; // HH:mm:ss

    const fullName = 'Null'; // Replace with actual value or logic to get full name
    const studentClass = 'Null'; // Replace with actual value or logic to get class
    const course = 'Null'; // Replace with actual value or logic to get course

    if (scanType === 'time_in') {
      const insertQuery = `INSERT INTO attendance_records (student_id, full_name, class, course, date_attend, time_in, time_out) VALUES (?, ?, ?, ?, ?, ?, '00:00:00')`;
      db.query(insertQuery, [studentID, fullName, studentClass, course, dateAttend, timeAttend], (err, result) => {
        if (err) {
          console.error(`Error inserting time_in: `, err);
          ws.send(`Error inserting time_in`);
        } else {
          console.log(`time_in recorded: ${timeAttend}`);
          ws.send(`time_in recorded: ${timeAttend}`);

          // Broadcast new record to all connected clients
          const newRecord = {
            id: result.insertId,
            student_id: studentID,
            full_name: fullName,
            class: studentClass,
            course: course,
            date_attend: dateAttend,
            time_in: timeAttend,
            time_out: '00:00:00'
          };
          wss.clients.forEach(client => {
            if (client.readyState === WebSocket.OPEN) {
              client.send(JSON.stringify(newRecord));
            }
          });
        }
      });
    } else if (scanType === 'time_out') {
      console.log(`Searching for time_out record: student_id=${studentID}, date_attend=${dateAttend}, time_out='00:00:00'`);
      
      // Truy vấn bản ghi mới nhất chưa có time_out cho studentID này
      const selectQuery = `
        SELECT * FROM attendance_records 
        WHERE student_id = ? AND time_out = '00:00:00'
          AND DATE(date_attend) = CURDATE() -- Lọc theo ngày hiện tại 
        ORDER BY time_in DESC 
        LIMIT 1`;
      
      db.query(selectQuery, [studentID], (err, results) => {
        if (err) {
          console.error(`Error querying for time_out: `, err);
          ws.send(`Error querying for time_out`);
        } else if (results.length === 0) {
          console.log(`No matching record found for time_out: student_id=${studentID}`);
          ws.send(`No matching record found for time_out: student_id=${studentID}`);
        } else {
          const updateQuery = `UPDATE attendance_records SET time_out = ? WHERE id = ?`;
          db.query(updateQuery, [timeAttend, results[0].id], err => {
            if (err) {
              console.error(`Error updating time_out: `, err);
              ws.send(`Error updating time_out`);
            } else {
              console.log(`time_out recorded: ${timeAttend}`);
              ws.send(`time_out recorded: ${timeAttend}`);

              // Broadcast updated record to all connected clients
              const updatedRecord = {
                id: results[0].id,
                student_id: studentID,
                full_name: results[0].full_name,
                class: results[0].class,
                course: results[0].course,
                date_attend: dateAttend,
                time_in: results[0].time_in,
                time_out: timeAttend
              };
              wss.clients.forEach(client => {
                if (client.readyState === WebSocket.OPEN) {
                  client.send(JSON.stringify(updatedRecord));
                }
              });
            }
          });
        }
      });
    } else {
      console.log(`Invalid scanType: ${scanType}`);
      ws.send(`Invalid scanType`);
    }
  });

  ws.on('close', () => {
    console.log('Client disconnected');
  });
});
