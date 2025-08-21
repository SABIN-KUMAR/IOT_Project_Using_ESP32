  #include <WiFi.h>
  #include <WebServer.h>
  #include <ElegantOTA.h>
  #include <Preferences.h>
  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  #include <OneWire.h>
  #include <DallasTemperature.h>
  #include <NewPing.h>

  // =================== Pin definitions ===================
  const int FLOW_SENSOR_PIN   = 32;
  const int RED_LED_PIN       = 12;
  const int YELLOW_LED_PIN    = 13;
  const int GREEN_LED_PIN     = 14;
  const int BUZZER_PIN        = 19;
  const int TRIG_PIN          = 5;
  const int ECHO_PIN          = 18;
  const int TEMP_SENSOR_PIN   = 4;

  // =================== LCD I2C details ===================
  const int LCD_ADDRESS = 0x27;       // Change to 0x3F if your module uses that
  const int LCD_COLS    = 16;
  const int LCD_ROWS    = 2;

  // =================== Tank / ultrasonic settings ===================
  const int MAX_DISTANCE_CM   = 15;   // Max measurable distance from sensor (cm)
  const int FUEL_TANK_HEIGHT  = 13;   // Tank height (cm)
  const int FUEL_TANK_OFFSET  = 0;    // Offset from sensor to full fuel surface (cm)

  // =================== Calibration ===================
  // Using user's original calibration: 7.5 pulses/mL = 7500 pulses/L
  const float PULSES_PER_LITER = 375.0f;

  // =================== Global objects ===================
  WebServer server(80);
  LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);
  Preferences preferences;
  OneWire oneWireBus(TEMP_SENSOR_PIN);
  DallasTemperature tempSensors(&oneWireBus);
  NewPing sonar(TRIG_PIN, ECHO_PIN, MAX_DISTANCE_CM);

  // =================== Globals (state) ===================
  volatile unsigned long flow_pulse_count = 0;   // pulses since last rate calc
  volatile unsigned long flow_pulse_total = 0;   // pulses since boot

  float current_temperature   = 0.0f;
  float current_flow_rate_Lpm = 0.0f;            // L/min
  float fuel_level_cm         = 0.0f;            // computed level (cm)

  unsigned long last_pulse_read_time = 0;        // ms
  unsigned long last_serial_print    = 0;        // ms

  bool   configMode      = false;
  String saved_ssid      = "";
  String saved_password  = "";

  // =============== ISR for the flow sensor =================
  void IRAM_ATTR flow_pulse_counter() {
    flow_pulse_count++;
    flow_pulse_total++;
  }

  // =================== HTML: Main UI ===================
  const char* HTML_CONTENT_MAIN = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>ESP32 Fuel Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      :root {
        --primary:#007bff; --secondary:#6c757d; --success:#28a745;
        --warning:#ffc107; --danger:#dc3545; --bg:#f8f9fa; --card:#fff; --text:#212529; --border:#dee2e6;
      }
      body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;background:var(--bg);margin:0;color:var(--text);display:flex;flex-direction:column;min-height:100vh}
      .header{background:var(--primary);color:#fff;padding:20px;text-align:center;box-shadow:0 2px 5px rgba(0,0,0,.2)}
      .container{flex-grow:1;padding:20px;max-width:900px;margin:20px auto}
      .tab-container{display:flex;border-bottom:1px solid var(--border);margin-bottom:20px}
      .tab-button{background:transparent;border:none;padding:10px 15px;cursor:pointer;font-size:1rem;color:var(--secondary);transition:.3s}
      .tab-button.active{color:var(--primary);border-bottom:3px solid var(--primary)}
      .tab-content{display:none}.tab-content.active{display:block}
      .card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:20px;margin-bottom:20px;box-shadow:0 4px 6px rgba(0,0,0,.1);text-align:center}
      .card h3{margin-top:0;color:var(--primary)} .card p{font-size:2.0rem;margin:10px 0 0;font-weight:bold}
      .status-light{width:50px;height:50px;border-radius:50%;background:var(--secondary);margin:20px auto;transition:background-color .5s}
      .status-light.red{background:var(--danger)} .status-light.yellow{background:var(--warning)} .status-light.green{background:var(--success)}
    </style>
  </head>
  <body>
    <div class="header">
      <h1>IOT-Based Smart Fuel Monitoring System</h1>
      <p>Current IP: %IP_ADDRESS%</p>
          <p>Supervisor: Er.Anil Kumar Gupta</p>
              <p>Presented By: Mr.Krishna Khagda</p>
              


    </div>
    <div class="container">
      <div class="tab-container">
        <button class="tab-button active" onclick="openTab(event,'Dashboard')">Dashboard</button>
        <button class="tab-button" onclick="openTab(event,'FuelStatus')">Fuel Status</button>
      </div>

      <div id="Dashboard" class="tab-content active">
        <div class="card"><h3>Flow Rate</h3><p><span id="flowRate">0.00</span> L/min</p></div>
        <div class="card"><h3>Total Fuel Passed</h3><p><span id="totalFlow">0.00</span> Liters</p></div>
        <div class="card"><h3>Petrol Temperature</h3><p><span id="temperature">0.0</span> &deg;C</p></div>
      </div>

      <div id="FuelStatus" class="tab-content">
        <div class="card"><h3>Fuel Level</h3><p><span id="fuelLevel">0.0</span> cm</p></div>
        <div class="card"><h3>Fuel Percentage</h3><p><span id="fuelPercentage">0</span> %</p></div>
        <div class="card"><h3>Status</h3><div id="fuelStatusLight" class="status-light"></div></div>
      </div>
    </div>

  <script>
  function openTab(evt, tabName){
    const tabs=document.getElementsByClassName("tab-content");
    for(let i=0;i<tabs.length;i++){tabs[i].style.display="none";}
    const btns=document.getElementsByClassName("tab-button");
    for(let i=0;i<btns.length;i++){btns[i].className=btns[i].className.replace(" active","");}
    document.getElementById(tabName).style.display="block";
    evt.currentTarget.className+=" active";
  }
  async function fetchData(){
    try{
      const r=await fetch('/api/data');
      const d=await r.json();
      document.getElementById('flowRate').textContent=d.flowRate.toFixed(2);
      document.getElementById('totalFlow').textContent=d.totalFlow.toFixed(2);
      document.getElementById('temperature').textContent=d.temperature.toFixed(1);
      document.getElementById('fuelLevel').textContent=d.fuelLevel.toFixed(1);
      document.getElementById('fuelPercentage').textContent=d.fuelPercentage.toFixed(0);
      const light=document.getElementById('fuelStatusLight');
      light.className='status-light';
    if(d.fuelPercentage > 60) {
    light.className += ' green';
  } else if(d.fuelPercentage >= 10) {
    light.className += ' yellow';
  } else {
    light.className += ' red';
  }

    }catch(e){console.error(e);}
  }
  setInterval(fetchData,2000); fetchData();
  </script>
  </body>
  </html>
  )rawliteral";

  // =================== HTML: Config Portal ===================
  const char* HTML_CONTENT_CONFIG = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>WiFi Setup</title><meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body{font-family:Arial,sans-serif;text-align:center;margin-top:50px}
      .container{max-width:400px;margin:auto;padding:20px;border:1px solid #ccc;border-radius:10px}
      input[type=text],input[type=password]{width:90%;padding:10px;margin:10px 0;border-radius:5px;border:1px solid #ccc}
      input[type=submit]{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer}
    </style>
  </head>
  <body>
    <div class="container">
      <h2>ESP32 WiFi Setup</h2>
      <form action="/save" method="get">
        <input type="text" name="ssid" placeholder="WiFi SSID" required><br>
        <input type="password" name="pass" placeholder="Password"><br>
        <input type="submit" value="Connect">
      </form>
    </div>
  </body>
  </html>
  )rawliteral";

  // =================== Forward declarations ===================
  void setup_sensors();
  void setup_main_server();
  void setup_config_portal();
  void connect_to_wifi(const String& ssid, const String& password);
  void read_sensors();
  void update_lcd_display();
  void update_status_lights(float fuel_percentage);
  float calculate_fuel_percentage();

  // =================== Helpers ===================
  float total_liters_since_boot() {
    return (float)flow_pulse_total / PULSES_PER_LITER;
  }

  // =================== Setup ===================
  void setup() {
    Serial.begin(115200);
    delay(100);

    // GPIO
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(YELLOW_LED_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // LCD
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Starting...");
    lcd.setCursor(0, 1);
    lcd.print("Fuel Monitor");

    // Sensors
    setup_sensors();

    // Load saved WiFi credentials
    preferences.begin("creds", false);
    saved_ssid     = preferences.getString("ssid", "");
    saved_password = preferences.getString("pass", "");
    preferences.end();

    if (saved_ssid.isEmpty()) {
      configMode = true;
      setup_config_portal();
      Serial.println("[INFO] No saved WiFi. Starting AP config portal.");
    } else {
      Serial.print("[INFO] Stored SSID: ");
      Serial.println(saved_ssid);
      connect_to_wifi(saved_ssid, saved_password);
      setup_main_server();
    }

    last_pulse_read_time = millis();
    last_serial_print    = millis();
  }

  // =================== Loop ===================
  void loop() {
    server.handleClient();

    if (!configMode) {
      ElegantOTA.loop();
      read_sensors();
      update_lcd_display();
      update_status_lights(calculate_fuel_percentage());
    }
  }

  // =================== Sensors setup ===================
  void setup_sensors() {
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flow_pulse_counter, RISING);

    tempSensors.begin();
  }

  // =================== Config portal ===================
  void setup_config_portal() {
    Serial.println("[AP] Starting AP Mode: SSID=ESP32_Config");
    WiFi.softAP("ESP32_Config", "");
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[AP] IP: "); Serial.println(apIP);

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AP: ESP32_Config");
    lcd.setCursor(0, 1); lcd.print(apIP.toString());

    server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", HTML_CONTENT_CONFIG);
    });

    server.on("/save", HTTP_GET, []() {
      String ssid = server.arg("ssid");
      String password = server.arg("pass");

      preferences.begin("creds", false);
      preferences.putString("ssid", ssid);
      preferences.putString("pass", password);
      preferences.end();

      Serial.println("[AP] Credentials saved. Restarting...");
      server.send(200, "text/html", "<h2>Credentials saved. Restarting...</h2>");
      delay(1500);
      ESP.restart();
    });

    server.begin();
  }

  // =================== WiFi connect ===================
  void connect_to_wifi(const String& ssid, const String& password) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Connecting to:");
    lcd.setCursor(0, 1); lcd.print(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Connected!");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
      lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    } else {
      Serial.println("[WiFi] Failed to connect. Clearing creds & rebooting to AP mode.");
      preferences.begin("creds", false);
      preferences.clear();
      preferences.end();
      ESP.restart();
    }
  }

  // =================== Main web server ===================
  void setup_main_server() {
    server.on("/", HTTP_GET, []() {
      String html = String(HTML_CONTENT_MAIN);
      html.replace("%IP_ADDRESS%", WiFi.localIP().toString());
      server.send(200, "text/html", html);
    });

    server.on("/api/data", HTTP_GET, []() {
      float perc = calculate_fuel_percentage();
      String json = String("{\"temperature\":") + String(current_temperature, 1) +
                    ",\"flowRate\":" + String(current_flow_rate_Lpm, 2) +
                    ",\"totalFlow\":" + String(total_liters_since_boot(), 2) +
                    ",\"fuelLevel\":" + String(fuel_level_cm, 1) +
                    ",\"fuelPercentage\":" + String(perc, 0) + "}";
      server.send(200, "application/json", json);
    });

    ElegantOTA.begin(&server); // Works with WebServer
    server.begin();
    Serial.println("[HTTP] Server started.");
  }

  // =================== Sensor reading & logging ===================
  void read_sensors() {
    unsigned long now = millis();

    // ---- Flow rate every 1s ----
    if (now - last_pulse_read_time >= 1000) {
      // L/min = (pulses in 1s / pulses per liter) * 60
      noInterrupts();
      unsigned long pulses = flow_pulse_count;
      flow_pulse_count = 0;
      interrupts();

      current_flow_rate_Lpm = ( (float)pulses / PULSES_PER_LITER ) * 60.0f;
      last_pulse_read_time = now;
    }

    // ---- Temperature ----
    tempSensors.requestTemperatures();
    current_temperature = tempSensors.getTempCByIndex(0);

    // ---- Ultrasonic & derived fuel level ----
    unsigned int duration_us = sonar.ping_median();           // us
    if (duration_us == 0) {
      fuel_level_cm = 0.0f; // reading error
    } else {
      float distance_cm = sonar.convert_cm(duration_us);
      fuel_level_cm = FUEL_TANK_HEIGHT - (distance_cm - FUEL_TANK_OFFSET);
      if (fuel_level_cm < 0) fuel_level_cm = 0;
      if (fuel_level_cm > FUEL_TANK_HEIGHT) fuel_level_cm = FUEL_TANK_HEIGHT;
    }

    // ---- Serial logging every 1s ----
    if (now - last_serial_print >= 1000) {
      last_serial_print = now;

      float totalL = total_liters_since_boot();
      float perc   = calculate_fuel_percentage();

      Serial.println(F("----- Sensor Data -----"));
      Serial.print(F("Flow Rate: "));    Serial.print(current_flow_rate_Lpm, 2); Serial.println(F(" L/min"));
      Serial.print(F("Total Fuel: "));   Serial.print(totalL, 3);                Serial.println(F(" L"));
      Serial.print(F("Temperature: "));  Serial.print(current_temperature, 1);   Serial.println(F(" C"));
      Serial.print(F("Fuel Level: "));   Serial.print(fuel_level_cm, 1);         Serial.println(F(" cm"));
      Serial.print(F("Fuel %: "));       Serial.print(perc, 0);                  Serial.println(F(" %"));
      Serial.println(F("-----------------------"));
    }
  }

  // =================== LCD rotation ===================
  void update_lcd_display() {
    static unsigned long last_lcd_update = 0;
    static int display_mode = 0;

    if (millis() - last_lcd_update < 3000) return;
    last_lcd_update = millis();

    lcd.clear();
    if (display_mode == 0) {
      lcd.setCursor(0, 0); lcd.print("Temp: "); lcd.print(current_temperature, 1); lcd.print("C");
      lcd.setCursor(0, 1); lcd.print("Flow: "); lcd.print(current_flow_rate_Lpm, 2); lcd.print("L/m");
    } else {
      lcd.setCursor(0, 0); lcd.print("Level: "); lcd.print(fuel_level_cm, 1); lcd.print("cm");
      lcd.setCursor(0, 1); lcd.print("Fuel%: "); lcd.print(calculate_fuel_percentage(), 0); lcd.print("%");
    }
    display_mode = (display_mode + 1) % 2;
  }

  // =================== LED & buzzer ===================
  void update_status_lights(float fuel_percentage) {
    if (fuel_percentage < 10.0f) {
      digitalWrite(RED_LED_PIN, HIGH);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, HIGH);  // low fuel alert
    } else if (fuel_percentage <= 60.0f) {
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, HIGH);
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
    } else {
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }


  // =================== Fuel % ===================
  float calculate_fuel_percentage() {
    if (fuel_level_cm < 0) return 0.0f;
    float percentage = (fuel_level_cm / (float)FUEL_TANK_HEIGHT) * 100.0f;
    if (percentage > 100.0f) return 100.0f;
    if (percentage < 0.0f)   return 0.0f;
    return percentage;
  }
