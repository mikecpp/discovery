# Project Requirement Document
- Use C++, cmake. no comment, private XXXX_
- Use WebServer + UVE3.
- Use KConfig for componnets management.

- Components
    - Web Server
        - port 8080
    - Icmp 
        - ping device  

- Backend main application
    - Use multi threads to discover current subnet device.
    - Then try to check deice type or OS. 
    - Continue to watch device status. 
    - Run browser after HTTP server start. 

- Frontend 
    - Vertical navigator.
    - Use card style to show device status (Green: on, Red: off). 
