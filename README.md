## Smart Home IoT Simulator
*in C programming language*

&nbsp;

### 📋 Project Description
---

This project is a simulation of a smart home environment where multiple IoT sensors collect and send data to a centralized system for processing. The system stores sensor data, generates statistics, and triggers alerts when specific thresholds are met. The simulator is designed using multiple processes and threads, communicating through named pipes, shared memory, message queues, and unnamed pipes to handle concurrent data flow efficiently.

&nbsp;

*System Overview*

- Sensors: Independent processes that generate and send data periodically.
- User Console: Allows users to interact with the system, request statistics, and manage alerts.
- System Manager: The main process that initializes and coordinates the entire system.
- Workers: Processes that handle requests from sensors and user consoles.
- Alerts Watcher: Monitors sensor values and sends real-time alerts to user consoles.

&nbsp;

*Inter-Process Communication (IPC)*

- Named Pipes (SENSOR_PIPE, CONSOLE_PIPE)
- Shared Memory (SHM) for storing sensor data and alerts
- Message Queues for alert notifications and user responses
- Unnamed Pipes for Worker communication

&nbsp;

*Project Guidelines*

1. Implement the System Manager to initialize pipes, shared memory, and message queues.
2. Develop the User Console for command input and real-time alerts.
3. Implement Sensor processes that generate periodic data.
4. Develop Worker processes to handle sensor data and user commands.
5. Implement the Dispatcher for efficient task assignment.
6. Implement Alerts Watcher to monitor values and send notifications.
7. Ensure proper synchronization using semaphores and mutexes.
8. Log all system events in log.txt for debugging and auditing.
9. Optimize memory usage and resource management.

&nbsp;

*Functionalities*

**Sensor**
- Generates and sends data at defined intervals.
- Sends data in the format: SENSOR_ID#KEY#VALUE
- Supports controlled termination using SIGINT.
- Logs the number of messages sent when SIGTSTP is received.

**User Console**
- Interactive menu-based system for managing and viewing data.
- Commands supported:
exit          - Close the User Console  
stats         - Display sensor statistics  
reset         - Reset all stored statistics  
sensors       - List active sensors  
add_alert     - Add a new alert rule  
remove_alert  - Remove an alert rule  
list_alerts   - Show all active alerts  
- Receives real-time alerts via message queues.

**System Manager**
- Initializes all system components.
- Reads configuration file parameters (e.g., queue size, max alerts).
- Creates Workers, Dispatcher, and Alerts Watcher processes.
- Cleans up resources upon receiving SIGINT.

**Worker**
- Processes sensor data and stores it in shared memory.
- Handles user commands and sends responses.
- Manages concurrency to prevent data corruption.

**Alerts Watcher**
- Continuously monitors sensor values.
- Triggers alerts if values exceed predefined limits.
- Sends real-time notifications to relevant User Consoles.

&nbsp;

---

**NOTE:** The project development has been completed, with most functionalities working. However, further improvements are needed, as some threads are not functioning correctly. This project was developed within the scope of a Computer Science course by Cláudia Torres and Maria João Rosa.
