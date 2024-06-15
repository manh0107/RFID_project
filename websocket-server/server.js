const express = require('express');
const path = require('path');
const WebSocket = require('ws');
const mysql = require('mysql');
const os = require('os');
const bodyParser = require('body-parser');

const app = express();
const port = 8080;

app.use(bodyParser.json());

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

app.delete('/api/delete_record/:id', (req, res) => {
  const id = req.params.id;
  const deleteQuery = 'DELETE FROM attendance_records WHERE id = ?';
  db.query(deleteQuery, [id], (err, result) => {
    if (err) {
      console.error('Failed to delete record: ', err);
      res.status(500).send({ success: false, message: 'Failed to delete record' });
    } else {
      res.send({ success: true });

      // Send delete action to all clients
      wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
          client.send(JSON.stringify({ action: 'delete', id }));
        }
      });
    }
  });
});

app.get('/api/card_list', (req, res) => {
  const query = `
      SELECT t1.*
      FROM attendance_records t1
      INNER JOIN (
          SELECT card_uid, MAX(id) AS max_id
          FROM attendance_records
          WHERE card_uid IS NOT NULL
          GROUP BY card_uid
      ) t2 ON t1.card_uid = t2.card_uid AND t1.id = t2.max_id
  `;
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

    if (messageStr === 'request_data') {
      const query = 'SELECT * FROM attendance_records';
      db.query(query, (err, results) => {
        if (err) {
          console.error('Failed to retrieve data: ', err);
          ws.send(JSON.stringify({ error: 'Failed to retrieve data' }));
        } else {
          ws.send(JSON.stringify(results));
        }
      });
      return;
    }

    // Ensure the message follows the expected format: cardUID;studentID;scanType;formattedTime
    const parts = messageStr.split(";");
    if (parts.length !== 4) {
      console.log(`Invalid message format: ${messageStr}`);
      ws.send(JSON.stringify({ error: 'Invalid message format. Expected format: cardUID;studentID;scanType;formattedTime' }));
      return;
    }

    const [cardUID, studentID, scanType, formattedTime] = parts;
    const [datePart, timePart] = formattedTime.split(" ");

    // Validate date and time parts
    if (!datePart || !timePart) {
      console.log(`Invalid date/time format: ${formattedTime}`);
      ws.send(JSON.stringify({ error: 'Invalid date/time format. Expected format: DD-MM-YYYY HH:MM:SS' }));
      return;
    }

    const [day, month, year] = datePart.split("-");
    if (!day || !month || !year) {
      console.log(`Invalid date format: ${datePart}`);
      ws.send(JSON.stringify({ error: 'Invalid date format. Expected format: DD-MM-YYYY' }));
      return;
    }

    const dateAttend = `${year}-${month}-${day}`;
    const timeAttend = timePart;

    const fullName = 'Null';
    const studentClass = 'Null';
    const course = 'Null';

    if (scanType === 'time_in') {
      const insertQuery = `INSERT INTO attendance_records (student_id, full_name, class, course, date_attend, time_in, time_out, card_uid) VALUES (?, ?, ?, ?, ?, ?, '00:00:00', ?)`;
      db.query(insertQuery, [studentID, fullName, studentClass, course, dateAttend, timeAttend, cardUID], (err, result) => {
        if (err) {
          console.error(`Error inserting time_in: `, err);
          ws.send(JSON.stringify({ error: 'Error inserting time_in' }));
        } else {
          console.log(`time_in recorded: ${timeAttend}`);
          ws.send(JSON.stringify({ message: `time_in recorded: ${timeAttend}` }));

          const newRecord = {
            id: result.insertId,
            student_id: studentID,
            full_name: fullName,
            class: studentClass,
            course: course,
            date_attend: dateAttend,
            time_in: timeAttend,
            time_out: '00:00:00',
            card_uid: cardUID  // Include cardUID in the record
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
      const selectQuery = `
        SELECT * FROM attendance_records 
        WHERE student_id = ? AND time_out = '00:00:00'
          AND DATE(date_attend) = CURDATE()
        ORDER BY time_in DESC 
        LIMIT 1`;
      db.query(selectQuery, [studentID], (err, results) => {
        if (err) {
          console.error(`Error querying for time_out: `, err);
          ws.send(JSON.stringify({ error: 'Error querying for time_out' }));
        } else if (results.length === 0) {
          console.log(`No matching record found for time_out: student_id=${studentID}`);
          ws.send(JSON.stringify({ error: `No matching record found for time_out: student_id=${studentID}` }));
        } else {
          const updateQuery = `UPDATE attendance_records SET time_out = ?, card_uid = ? WHERE id = ?`;
          db.query(updateQuery, [timeAttend, cardUID, results[0].id], err => {
            if (err) {
              console.error(`Error updating time_out: `, err);
              ws.send(JSON.stringify({ error: 'Error updating time_out' }));
            } else {
              console.log(`time_out recorded: ${timeAttend}`);
              ws.send(JSON.stringify({ message: `time_out recorded: ${timeAttend}` }));

              const updatedRecord = {
                id: results[0].id,
                student_id: studentID,
                full_name: results[0].full_name,
                class: results[0].class,
                course: results[0].course,
                date_attend: dateAttend,
                time_in: results[0].time_in,
                time_out: timeAttend,
                card_uid: cardUID  // Include cardUID in the record
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
      ws.send(JSON.stringify({ error: 'Invalid scanType. Expected scanType: time_in or time_out' }));
    }
  });

  ws.on('close', () => {
    console.log('Client disconnected');
  });
});
