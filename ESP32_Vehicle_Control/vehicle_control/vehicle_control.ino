/*
  ============================================================
                GPSAgri Agriculture Vehicle
                         V4.1 HTTP
                 SERVO PWM CONFLICT FIXED
  ============================================================

  ARCHITECTURE

  PHONE 2 GPSAgri DASHBOARD
             |
             | HTTP / WiFi
             v
        ESP32 VEHICLE
             |
       +-----+-----+
       |           |
     L298N       SERVO
       |
    +--+--+
    |     |
   LEFT  RIGHT
  MOTOR  MOTOR

  ============================================================
  HARDWARE
  ============================================================

  ESP32
  L298N
  2 Rear DC Gear Motors
  Front 360 Degree Caster Wheel
  Servo
  DHT11
  Soil Moisture Sensor
  MQ135

  ============================================================
  DASHBOARD FUNCTIONS
  ============================================================

  Manual control
  Forward
  Back
  Left
  Right
  Stop

  Differential steering
  Speed control
  Steering sensitivity
  Draw-path following

  Servo:
  UP
  DOWN
  HOLD
  Angle slider

  Sensors:
  Temperature
  Humidity
  Soil moisture
  MQ135

  ============================================================
  IMPORTANT
  ============================================================

  NO UBIDOTS
  NO MQTT
  NO GPS

  Phone 2 communicates directly with this ESP32
  using HTTP.

  ============================================================
  V4.1 PWM FIX
  ============================================================

  SERVO:
      GPIO2
      LEDC TIMER 0

  LEFT MOTOR PWM:
      GPIO25
      LEDC CHANNEL 4
      TIMER 1

  RIGHT MOTOR PWM:
      GPIO33
      LEDC CHANNEL 5
      TIMER 1

  This keeps servo PWM separate from motor PWM.

  ESP32 Arduino Core 3.x API.
  ============================================================
*/


#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <ESP32Servo.h>


// ============================================================
// WIFI SETTINGS
// ============================================================

const char* WIFI_SSID =
    "USERID";

const char* WIFI_PASSWORD =
    "PASSWORD";


// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);


// ============================================================
// MOTOR PINS
// ============================================================

// LEFT REAR MOTOR

#define ENA 25
#define IN1 26
#define IN2 27


// RIGHT REAR MOTOR

#define ENB 33
#define IN3 32
#define IN4 14


// ============================================================
// MOTOR PWM
// ============================================================

#define PWM_FREQ       1000
#define PWM_RESOLUTION 8

// IMPORTANT:
//
// Do NOT use automatic ledcAttach() here.
//
// Servo uses TIMER 0.
// Motors use channels 4 and 5.
//
// Channels 4 and 5 belong to another LEDC timer.
//

#define LEFT_PWM_CHANNEL  4
#define RIGHT_PWM_CHANNEL 5


// ============================================================
// SERVO
// ============================================================

#define SERVO_PIN 2

#define SERVO_UP_ANGLE   10
#define SERVO_DOWN_ANGLE 100

Servo soilServo;


// ============================================================
// DHT11
// ============================================================

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(
    DHTPIN,
    DHTTYPE
);


// ============================================================
// ANALOG SENSORS
// ============================================================

#define SOIL_PIN 34
#define MQ135_PIN 35


// ============================================================
// VEHICLE SETTINGS
// ============================================================

int vehicleSpeed =
    180;

int steeringSensitivity =
    60;

int currentServoAngle =
    SERVO_UP_ANGLE;


// ============================================================
// MOTOR STATE
// ============================================================

int leftMotorSpeed =
    0;

int rightMotorSpeed =
    0;


// ============================================================
// SAFETY
// ============================================================

unsigned long lastMotorCommand =
    0;

const unsigned long MOTOR_TIMEOUT =
    1500;


// ============================================================
// SENSOR VALUES
// ============================================================

float temperature =
    0.0;

float humidity =
    0.0;

int soilRaw =
    0;

int mq135Raw =
    0;

int soilPercent =
    0;


// ============================================================
// SENSOR TIMING
// ============================================================

unsigned long lastSensorRead =
    0;

const unsigned long SENSOR_INTERVAL =
    2000;


// ============================================================
// WIFI RECONNECT TIMING
// ============================================================

unsigned long lastWiFiAttempt =
    0;

const unsigned long WIFI_RETRY_INTERVAL =
    10000;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void connectWiFi();

void maintainWiFi();

void setupWebServer();

void addCORS();

void stopMotors();

void moveForward();

void moveBackward();

void turnLeft();

void turnRight();

void steerVehicle(
    float x
);

void setMotor(
    int leftSpeed,
    int rightSpeed
);

void setServoAngle(
    int angle
);

void servoUp();

void servoDown();

void servoHold();

void readSensors();

String getSensorJSON();

String getHTML();


// ============================================================
// SETUP
// ============================================================

void setup()
{

    Serial.begin(
        115200
    );

    delay(
        1000
    );


    Serial.println();
    Serial.println();

    Serial.println(
        "================================================"
    );

    Serial.println(
        "       GPSAgri Agriculture Vehicle V4.1"
    );

    Serial.println(
        "        SERVO PWM CONFLICT FIXED"
    );

    Serial.println(
        "================================================"
    );


    // ========================================================
    // MOTOR DIRECTION PINS
    // ========================================================

    pinMode(
        IN1,
        OUTPUT
    );

    pinMode(
        IN2,
        OUTPUT
    );

    pinMode(
        IN3,
        OUTPUT
    );

    pinMode(
        IN4,
        OUTPUT
    );


    // ========================================================
    // MOTOR PWM
    // ========================================================
    //
    // IMPORTANT:
    //
    // Servo uses LEDC timer 0.
    //
    // Motors use LEDC channels 4 and 5.
    //
    // Therefore the motor PWM resources are separated
    // from the servo PWM resources.
    //
    // ESP32 Arduino Core 3.x:
    //
    // ledcAttachChannel(pin, frequency, resolution, channel)
    //
    // ========================================================

    Serial.println(
        "Initializing motor PWM..."
    );


    bool leftPWMOK =
        ledcAttachChannel(
            ENA,
            PWM_FREQ,
            PWM_RESOLUTION,
            LEFT_PWM_CHANNEL
        );


    bool rightPWMOK =
        ledcAttachChannel(
            ENB,
            PWM_FREQ,
            PWM_RESOLUTION,
            RIGHT_PWM_CHANNEL
        );


    Serial.print(
        "Left PWM attach: "
    );

    Serial.println(
        leftPWMOK ? "OK" : "FAILED"
    );


    Serial.print(
        "Right PWM attach: "
    );

    Serial.println(
        rightPWMOK ? "OK" : "FAILED"
    );


    // Start motors stopped

    ledcWrite(
        ENA,
        0
    );

    ledcWrite(
        ENB,
        0
    );


    digitalWrite(
        IN1,
        LOW
    );

    digitalWrite(
        IN2,
        LOW
    );

    digitalWrite(
        IN3,
        LOW
    );

    digitalWrite(
        IN4,
        LOW
    );


    // ========================================================
    // SERVO
    // ========================================================

    Serial.println(
        "Initializing servo..."
    );


    // ========================================================
    // IMPORTANT SERVO FIX
    //
    // Allocate ONLY TIMER 0.
    //
    // Do NOT allocate all four timers.
    //
    // Motor PWM is placed on channels 4 and 5.
    // ========================================================

    ESP32PWM::allocateTimer(0);


    soilServo.setPeriodHertz(
        50
    );


    int servoResult =
        soilServo.attach(
            SERVO_PIN,
            500,
            2400
        );


    Serial.print(
        "Servo attach result: "
    );

    Serial.println(
        servoResult
    );


    if (servoResult)
    {

        Serial.println(
            "SERVO ATTACHED OK"
        );

    }
    else
    {

        Serial.println(
            "SERVO ATTACH FAILED!"
        );

    }


    currentServoAngle =
        SERVO_UP_ANGLE;


    soilServo.write(
        currentServoAngle
    );


    delay(
        500
    );


    Serial.print(
        "Servo initial angle: "
    );

    Serial.println(
        currentServoAngle
    );


    // ========================================================
    // DHT
    // ========================================================

    dht.begin();


    // ========================================================
    // INITIAL SENSOR READ
    // ========================================================

    readSensors();


    // ========================================================
    // WIFI
    // ========================================================

    connectWiFi();


    // ========================================================
    // WEB SERVER
    // ========================================================

    setupWebServer();

    server.begin();


    Serial.println();

    Serial.println(
        "HTTP server started"
    );


    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {

        Serial.print(
            "Open ESP32 at: http://"
        );

        Serial.println(
            WiFi.localIP()
        );

    }


    // ========================================================
    // INITIAL SAFETY TIMER
    // ========================================================

    lastMotorCommand =
        millis();


    Serial.println();

    Serial.println(
        "GPSAgri V4.1 controller READY"
    );

    Serial.println(
        "Servo PWM isolated from motor PWM"
    );

}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{

    // ========================================================
    // HANDLE HTTP REQUESTS
    // ========================================================

    server.handleClient();


    // ========================================================
    // WIFI MAINTENANCE
    // ========================================================

    maintainWiFi();


    // ========================================================
    // SENSOR UPDATE
    // ========================================================

    if (
        millis() -
        lastSensorRead >=
        SENSOR_INTERVAL
    )
    {

        lastSensorRead =
            millis();

        readSensors();

    }


    // ========================================================
    // MOTOR SAFETY
    // ========================================================

    if (
        millis() -
        lastMotorCommand >
        MOTOR_TIMEOUT
    )
    {

        if (
            leftMotorSpeed != 0 ||
            rightMotorSpeed != 0
        )
        {

            Serial.println(
                "SAFETY TIMEOUT -> STOP"
            );

            stopMotors();

        }

    }

}


// ============================================================
// WIFI CONNECTION
// ============================================================

void connectWiFi()
{

    Serial.println();
    Serial.print(
        "Connecting to WiFi: "
    );

    Serial.println(
        WIFI_SSID
    );


    WiFi.mode(
        WIFI_STA
    );


    WiFi.setAutoReconnect(
        true
    );


    WiFi.persistent(
        false
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    int attempts =
        0;


    while (
        WiFi.status() !=
        WL_CONNECTED &&
        attempts < 40
    )
    {

        delay(
            500
        );

        Serial.print(
            "."
        );

        attempts++;

    }


    Serial.println();


    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {

        Serial.println(
            "WiFi CONNECTED"
        );


        Serial.print(
            "SSID: "
        );

        Serial.println(
            WiFi.SSID()
        );


        Serial.print(
            "ESP32 IP: "
        );

        Serial.println(
            WiFi.localIP()
        );


        Serial.print(
            "Signal RSSI: "
        );

        Serial.print(
            WiFi.RSSI()
        );

        Serial.println(
            " dBm"
        );

    }

    else
    {

        Serial.println(
            "WiFi connection FAILED"
        );

    }

}


// ============================================================
// WIFI MAINTENANCE
// ============================================================

void maintainWiFi()
{

    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {

        return;

    }


    if (
        millis() -
        lastWiFiAttempt <
        WIFI_RETRY_INTERVAL
    )
    {

        return;

    }


    lastWiFiAttempt =
        millis();


    Serial.println(
        "WiFi disconnected."
    );

    Serial.println(
        "Trying to reconnect..."
    );


    WiFi.disconnect();

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

}


// ============================================================
// CORS
// ============================================================

void addCORS()
{

    server.sendHeader(
        "Access-Control-Allow-Origin",
        "*"
    );


    server.sendHeader(
        "Access-Control-Allow-Methods",
        "GET,OPTIONS"
    );


    server.sendHeader(
        "Access-Control-Allow-Headers",
        "*"
    );


    server.sendHeader(
        "Cache-Control",
        "no-store"
    );

}


// ============================================================
// MOTOR CONTROL
// ============================================================

void setMotor(
    int leftSpeed,
    int rightSpeed
)
{

    // ========================================================
    // SAVE MOTOR STATE
    // ========================================================

    leftMotorSpeed =
        leftSpeed;


    rightMotorSpeed =
        rightSpeed;


    // ========================================================
    // LEFT MOTOR DIRECTION
    // ========================================================

    if (
        leftSpeed > 0
    )
    {

        digitalWrite(
            IN1,
            HIGH
        );

        digitalWrite(
            IN2,
            LOW
        );

    }

    else if (
        leftSpeed < 0
    )
    {

        digitalWrite(
            IN1,
            LOW
        );

        digitalWrite(
            IN2,
            HIGH
        );

    }

    else
    {

        digitalWrite(
            IN1,
            LOW
        );

        digitalWrite(
            IN2,
            LOW
        );

    }


    // ========================================================
    // RIGHT MOTOR DIRECTION
    // ========================================================

    if (
        rightSpeed > 0
    )
    {

        digitalWrite(
            IN3,
            HIGH
        );

        digitalWrite(
            IN4,
            LOW
        );

    }

    else if (
        rightSpeed < 0
    )
    {

        digitalWrite(
            IN3,
            LOW
        );

        digitalWrite(
            IN4,
            HIGH
        );

    }

    else
    {

        digitalWrite(
            IN3,
            LOW
        );

        digitalWrite(
            IN4,
            LOW
        );

    }


    // ========================================================
    // PWM
    // ========================================================

    int leftPWM =
        constrain(
            abs(leftSpeed),
            0,
            255
        );


    int rightPWM =
        constrain(
            abs(rightSpeed),
            0,
            255
        );


    // IMPORTANT:
    // Use the GPIO pins here.
    // They are already assigned to fixed channels
    // by ledcAttachChannel().

    ledcWrite(
        ENA,
        leftPWM
    );


    ledcWrite(
        ENB,
        rightPWM
    );

}


// ============================================================
// FORWARD
// ============================================================

void moveForward()
{

    setMotor(
        vehicleSpeed,
        vehicleSpeed
    );


    lastMotorCommand =
        millis();


    Serial.print(
        "FORWARD  L="
    );

    Serial.print(
        vehicleSpeed
    );

    Serial.print(
        " R="
    );

    Serial.println(
        vehicleSpeed
    );

}


// ============================================================
// BACKWARD
// ============================================================

void moveBackward()
{

    setMotor(
        -vehicleSpeed,
        -vehicleSpeed
    );


    lastMotorCommand =
        millis();


    Serial.print(
        "BACKWARD  L="
    );

    Serial.print(
        -vehicleSpeed
    );

    Serial.print(
        " R="
    );

    Serial.println(
        -vehicleSpeed
    );

}


// ============================================================
// LEFT
// ============================================================

void turnLeft()
{

    int turningSpeed =
        vehicleSpeed;


    setMotor(
        0,
        turningSpeed
    );


    lastMotorCommand =
        millis();


    Serial.print(
        "LEFT  L=0 R="
    );

    Serial.println(
        turningSpeed
    );

}


// ============================================================
// RIGHT
// ============================================================

void turnRight()
{

    int turningSpeed =
        vehicleSpeed;


    setMotor(
        turningSpeed,
        0
    );


    lastMotorCommand =
        millis();


    Serial.print(
        "RIGHT  L="
    );

    Serial.print(
        turningSpeed
    );

    Serial.println(
        " R=0"
    );

}


// ============================================================
// PROPORTIONAL STEERING
// ============================================================

void steerVehicle(
    float x
)
{

    // ========================================================
    // LIMIT X
    // ========================================================

    x =
        constrain(
            x,
            -1.0,
            1.0
        );


    // ========================================================
    // APPLY STEERING SENSITIVITY
    // ========================================================

    float sensitivity =
        steeringSensitivity /
        100.0;


    x *=
        sensitivity;


    // ========================================================
    // DEAD BAND
    // ========================================================

    if (
        fabs(x) < 0.08
    )
    {

        x =
            0;

    }


    // ========================================================
    // BASE SPEED
    // ========================================================

    int base =
        vehicleSpeed;


    int leftPWM =
        base;


    int rightPWM =
        base;


    // ========================================================
    // LEFT
    // ========================================================

    if (
        x < 0
    )
    {

        float amount =
            -x;


        leftPWM =
            (int)(
                base *
                (1.0 - amount)
            );


        rightPWM =
            base;

    }


    // ========================================================
    // RIGHT
    // ========================================================

    else if (
        x > 0
    )
    {

        float amount =
            x;


        leftPWM =
            base;


        rightPWM =
            (int)(
                base *
                (1.0 - amount)
            );

    }


    // ========================================================
    // CENTER
    // ========================================================

    else
    {

        leftPWM =
            base;

        rightPWM =
            base;

    }


    // ========================================================
    // LIMIT PWM
    // ========================================================

    leftPWM =
        constrain(
            leftPWM,
            0,
            255
        );


    rightPWM =
        constrain(
            rightPWM,
            0,
            255
        );


    // ========================================================
    // APPLY
    // ========================================================

    setMotor(
        leftPWM,
        rightPWM
    );


    lastMotorCommand =
        millis();


    Serial.print(
        "STEER X="
    );

    Serial.print(
        x,
        3
    );

    Serial.print(
        "  L="
    );

    Serial.print(
        leftPWM
    );

    Serial.print(
        "  R="
    );

    Serial.println(
        rightPWM
    );

}


// ============================================================
// STOP
// ============================================================

void stopMotors()
{

    // ========================================================
    // PWM OFF
    // ========================================================

    ledcWrite(
        ENA,
        0
    );

    ledcWrite(
        ENB,
        0
    );


    // ========================================================
    // MOTOR DIRECTION OFF
    // ========================================================

    digitalWrite(
        IN1,
        LOW
    );

    digitalWrite(
        IN2,
        LOW
    );

    digitalWrite(
        IN3,
        LOW
    );

    digitalWrite(
        IN4,
        LOW
    );


    // ========================================================
    // STATE
    // ========================================================

    leftMotorSpeed =
        0;

    rightMotorSpeed =
        0;


    Serial.println(
        "MOTORS STOPPED"
    );

}


// ============================================================
// SERVO
// ============================================================

void setServoAngle(
    int angle
)
{

    angle =
        constrain(
            angle,
            0,
            180
        );


    currentServoAngle =
        angle;


    soilServo.write(
        currentServoAngle
    );


    Serial.print(
        "SERVO ANGLE = "
    );

    Serial.println(
        currentServoAngle
    );

}


// ============================================================
// SERVO UP
// ============================================================

void servoUp()
{

    setServoAngle(
        SERVO_UP_ANGLE
    );

}


// ============================================================
// SERVO DOWN
// ============================================================

void servoDown()
{

    setServoAngle(
        SERVO_DOWN_ANGLE
    );

}


// ============================================================
// SERVO HOLD
// ============================================================

void servoHold()
{

    soilServo.write(
        currentServoAngle
    );


    Serial.print(
        "SERVO HOLD = "
    );

    Serial.println(
        currentServoAngle
    );

}


// ============================================================
// READ SENSORS
// ============================================================

void readSensors()
{

    // ========================================================
    // DHT11
    // ========================================================

    float newTemperature =
        dht.readTemperature();


    float newHumidity =
        dht.readHumidity();


    if (
        !isnan(
            newTemperature
        )
    )
    {

        temperature =
            newTemperature;

    }


    if (
        !isnan(
            newHumidity
        )
    )
    {

        humidity =
            newHumidity;

    }


    // ========================================================
    // SOIL
    // ========================================================

    soilRaw =
        analogRead(
            SOIL_PIN
        );


    // ========================================================
    // MQ135
    // ========================================================

    mq135Raw =
        analogRead(
            MQ135_PIN
        );


    // ========================================================
    // SOIL CALIBRATION
    // ========================================================

    const int SOIL_DRY =
        4095;

    const int SOIL_WET =
        1200;


    soilPercent =
        map(
            soilRaw,
            SOIL_DRY,
            SOIL_WET,
            0,
            100
        );


    soilPercent =
        constrain(
            soilPercent,
            0,
            100
        );

}


// ============================================================
// SENSOR JSON
// ============================================================

String getSensorJSON()
{

    String json =
        "{";


    json +=
        "\"temperature\":";


    json +=
        String(
            temperature,
            1
        );


    json +=
        ",\"humidity\":";


    json +=
        String(
            humidity,
            1
        );


    json +=
        ",\"soil\":";


    json +=
        String(
            soilPercent
        );


    json +=
        ",\"soil_raw\":";


    json +=
        String(
            soilRaw
        );


    json +=
        ",\"air_quality\":";


    json +=
        String(
            mq135Raw
        );


    json +=
        ",\"vehicle_speed\":";


    json +=
        String(
            vehicleSpeed
        );


    json +=
        ",\"steering_sensitivity\":";


    json +=
        String(
            steeringSensitivity
        );


    json +=
        ",\"servo_angle\":";


    json +=
        String(
            currentServoAngle
        );


    json +=
        ",\"left_motor\":";


    json +=
        String(
            leftMotorSpeed
        );


    json +=
        ",\"right_motor\":";


    json +=
        String(
            rightMotorSpeed
        );


    json +=
        "}";


    return json;

}


// ============================================================
// WEB SERVER
// ============================================================

void setupWebServer()
{

    // ========================================================
    // ROOT
    // ========================================================

    server.on(
        "/",
        HTTP_GET,
        []()
        {

            addCORS();


            server.send(
                200,
                "text/html",
                getHTML()
            );

        }
    );


    // ========================================================
    // OPTIONS
    // ========================================================

    server.on(
        "/",
        HTTP_OPTIONS,
        []()
        {

            addCORS();

            server.send(
                204
            );

        }
    );


    // ========================================================
    // FORWARD
    // ========================================================

    server.on(
        "/forward",
        HTTP_GET,
        []()
        {

            addCORS();


            moveForward();


            server.send(
                200,
                "text/plain",
                "FORWARD"
            );

        }
    );


    // ========================================================
    // BACK
    // ========================================================

    server.on(
        "/back",
        HTTP_GET,
        []()
        {

            addCORS();


            moveBackward();


            server.send(
                200,
                "text/plain",
                "BACK"
            );

        }
    );


    // ========================================================
    // LEFT
    // ========================================================

    server.on(
        "/left",
        HTTP_GET,
        []()
        {

            addCORS();


            turnLeft();


            server.send(
                200,
                "text/plain",
                "LEFT"
            );

        }
    );


    // ========================================================
    // RIGHT
    // ========================================================

    server.on(
        "/right",
        HTTP_GET,
        []()
        {

            addCORS();


            turnRight();


            server.send(
                200,
                "text/plain",
                "RIGHT"
            );

        }
    );


    // ========================================================
    // STOP
    // ========================================================

    server.on(
        "/stop",
        HTTP_GET,
        []()
        {

            addCORS();


            stopMotors();


            lastMotorCommand =
                millis();


            server.send(
                200,
                "text/plain",
                "STOP"
            );

        }
    );


    // ========================================================
    // STEER
    // ========================================================

    server.on(
        "/steer",
        HTTP_GET,
        []()
        {

            addCORS();


            if (
                !server.hasArg(
                    "x"
                )
            )
            {

                server.send(
                    400,
                    "text/plain",
                    "Missing x"
                );

                return;

            }


            float x =
                server.arg(
                    "x"
                ).toFloat();


            steerVehicle(
                x
            );


            server.send(
                200,
                "text/plain",
                "STEER"
            );

        }
    );


    // ========================================================
    // SPEED
    // ========================================================

    server.on(
        "/speed",
        HTTP_GET,
        []()
        {

            addCORS();


            if (
                !server.hasArg(
                    "value"
                )
            )
            {

                server.send(
                    400,
                    "text/plain",
                    "Missing value"
                );

                return;

            }


            int value =
                server.arg(
                    "value"
                ).toInt();


            vehicleSpeed =
                constrain(
                    value,
                    0,
                    255
                );


            Serial.print(
                "SPEED = "
            );

            Serial.println(
                vehicleSpeed
            );


            server.send(
                200,
                "text/plain",
                String(
                    vehicleSpeed
                )
            );

        }
    );


    // ========================================================
    // STEERING SENSITIVITY
    // ========================================================

    server.on(
        "/sensitivity",
        HTTP_GET,
        []()
        {

            addCORS();


            if (
                !server.hasArg(
                    "value"
                )
            )
            {

                server.send(
                    400,
                    "text/plain",
                    "Missing value"
                );

                return;

            }


            int value =
                server.arg(
                    "value"
                ).toInt();


            steeringSensitivity =
                constrain(
                    value,
                    0,
                    100
                );


            Serial.print(
                "STEERING SENSITIVITY = "
            );

            Serial.println(
                steeringSensitivity
            );


            server.send(
                200,
                "text/plain",
                String(
                    steeringSensitivity
                )
            );

        }
    );


    // ========================================================
    // SERVO UP
    // ========================================================

    server.on(
        "/servo_up",
        HTTP_GET,
        []()
        {

            addCORS();


            servoUp();


            server.send(
                200,
                "text/plain",
                "SERVO UP"
            );

        }
    );


    // ========================================================
    // SERVO DOWN
    // ========================================================

    server.on(
        "/servo_down",
        HTTP_GET,
        []()
        {

            addCORS();


            servoDown();


            server.send(
                200,
                "text/plain",
                "SERVO DOWN"
            );

        }
    );


    // ========================================================
    // SERVO HOLD
    // ========================================================

    server.on(
        "/servo_hold",
        HTTP_GET,
        []()
        {

            addCORS();


            servoHold();


            server.send(
                200,
                "text/plain",
                "SERVO HOLD"
            );

        }
    );


    // ========================================================
    // SERVO ANGLE
    // ========================================================

    server.on(
        "/servo",
        HTTP_GET,
        []()
        {

            addCORS();


            if (
                !server.hasArg(
                    "angle"
                )
            )
            {

                server.send(
                    400,
                    "text/plain",
                    "Missing angle"
                );

                return;

            }


            int angle =
                server.arg(
                    "angle"
                ).toInt();


            setServoAngle(
                angle
            );


            server.send(
                200,
                "text/plain",
                String(
                    currentServoAngle
                )
            );

        }
    );


    // ========================================================
    // SENSORS
    // ========================================================

    server.on(
        "/sensors",
        HTTP_GET,
        []()
        {

            addCORS();


            readSensors();


            String json =
                getSensorJSON();


            server.send(
                200,
                "application/json",
                json
            );

        }
    );


    // ========================================================
    // NOT FOUND
    // ========================================================

    server.onNotFound(
        []()
        {

            addCORS();


            server.send(
                404,
                "text/plain",
                "GPSAgri: endpoint not found"
            );

        }
    );

}


// ============================================================
// SIMPLE ESP32 STATUS PAGE
// ============================================================

String getHTML()
{

    String html;


    html +=
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>GPSAgri ESP32</title>"
        "</head>";


    html +=
        "<body "
        "style='font-family:Arial;text-align:center;"
        "background:#07110a;color:white;'>";


    html +=
        "<h1>🌱 GPSAgri ESP32</h1>";


    html +=
        "<h2>Vehicle Controller V4.1</h2>";


    html +=
        "<p>HTTP Controller: ONLINE</p>";


    html +=
        "<p>IP Address: <b>";


    html +=
        WiFi.localIP().toString();


    html +=
        "</b></p>";


    html +=
        "<p>Speed: ";


    html +=
        String(
            vehicleSpeed
        );


    html +=
        "</p>";


    html +=
        "<p>Steering Sensitivity: ";


    html +=
        String(
            steeringSensitivity
        );


    html +=
        "</p>";


    html +=
        "<p>Servo: ";


    html +=
        String(
            currentServoAngle
        );


    html +=
        " degrees</p>";


    html +=
        "<p>Temperature: ";


    html +=
        String(
            temperature,
            1
        );


    html +=
        " C</p>";


    html +=
        "<p>Humidity: ";


    html +=
        String(
            humidity,
            1
        );


    html +=
        " %</p>";


    html +=
        "<p>Soil: ";


    html +=
        String(
            soilPercent
        );


    html +=
        " %</p>";


    html +=
        "<p>MQ135: ";


    html +=
        String(
            mq135Raw
        );


    html +=
        "</p>";


    html +=
        "</body>"
        "</html>";


    return html;

}