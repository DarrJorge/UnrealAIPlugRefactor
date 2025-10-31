# 🧠 AiAssistant Plugin (Unreal Engine 5.7)

AiAssistant is a plugin for Unreal Engine 5.7 that provides an infrastructure for executing and integrating AI-driven Python commands directly within the engine.
The project features a modular and extensible command-execution architecture designed for flexibility and integration with various UE subsystems.

## 🔧 Recent Refactoring Highlights

The latest updates significantly improved the plugin’s architecture and code maintainability:
- Simplified Python command execution logic, improved error handling, and enhanced thread safety within Unreal Engine.
- Introduced a strict ownership model to prevent unwanted object copying and improve resource safety.
- Implemented the Command design pattern for a unified execution interface. Each command is now isolated and easily extendable.
- Reorganized the project structure with dedicated subfolders for better source and test organization.
- Rewrote the URL parsing function using regular expressions, improving accuracy and readability.

## 🧩 Features

- Execute Python scripts directly from Unreal Engine  
- Modular command-based architecture (Command Pattern)  
- Extensible execution interface  
- Safe resource and process handling  
- Clean and logical project structure  


## 🧱 Technologies
- Unreal Engine 5.7
- C++20
- Python Integration
- Design Patterns: Command, NonCopyable
