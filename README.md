# IoT-Based Physical Cyber Security Alarm and Incident Response System

SOC analysts deal with hundreds of alerts every day. Most of them get ignored — not because they're unimportant, but because staring at a screen all day kills focus. This project takes a different approach: instead of yet another dashboard notification, it makes the alarm physical and impossible to ignore. The station announces the attacker's IP and location out loud, keeps beeping, and only shuts up when you physically walk up and press the button.

---

## How It Works

A web-based simulator sends a fake attack log to an n8n automation workflow. The workflow picks up the source IP, queries AbuseIPDB for threat intelligence, and passes the result through a Groq LLM to extract the risk score and country. That data gets converted into a Turkish voice announcement via Google TTS and pushed over HTTP to the ESP32-S3 device. The device plays the audio through the speaker, displays attacker info on the OLED screen, and keeps the alarm going until the operator presses the physical button. Once acknowledged, the incident gets logged to Google Sheets and a report is sent via email.


Hardware
| Component | Details |
| Deneyap Kart V2 | ESP32-S3, dual-core |
| OLED Display | 128x64, I2C (SSD1306) |
| Speaker Module | I2S audio output (no DAC) |
| SHT4x Sensor | Temperature & humidity |
| Push Button | Incident acknowledgment |

 Project Structure

soc-physical-alarm-system/
├── firmware/
│   └── alarm_station.ino
├── web/
│   └── attack_simulator.html
├── automation/
│   └── n8n_workflow.json
├── hardware/
│   └── circuit_diagram.jpg
└── README.md


## Setup

**Firmware**
- Open `firmware/alarm_station.ino` in Arduino IDE
- Set your WiFi credentials and server IP
- Flash to Deneyap Kart V2

**Automation**
- Run [n8n](https://n8n.io/) locally or on a server
- Import `automation/n8n_workflow.json`
- Add your API keys: Groq, AbuseIPDB, Gmail SMTP, Google Sheets

**Simulator**
- Open `web/attack_simulator.html` in a browser
- Update `N8N_WEBHOOK_URL` with your n8n address
- Hit the button and watch it go

---

## Stack

- **Edge:** ESP32-S3, FreeRTOS, ESPAsyncWebServer, U8g2, ESP8266Audio, Adafruit SHT4x
- **Automation:** n8n
- **AI:** Groq — Llama 3.3 70B
- **Threat Intel:** AbuseIPDB
- **TTS:** Google Translate TTS
- **Storage:** Google Sheets, Gmail
- **Frontend:** HTML, JavaScript, Chart.js

---

## License

MIT

---

**Harun DUZAK** — Bilecik Şeyh Edebali University, Computer Engineering  
[github.com/harunduzak](https://github.com/harunduzak)
