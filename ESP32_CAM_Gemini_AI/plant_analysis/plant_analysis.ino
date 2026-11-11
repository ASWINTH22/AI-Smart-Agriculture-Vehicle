/*
============================================================
                    GPSAGRI ESP32-CAM
              CAMERA + GEMINI AI SERVER
============================================================

BOARD:
    AI Thinker ESP32-CAM

NO:
    - Ubidots
    - MQTT
    - GPS

FEATURES:
    - Camera capture
    - Latest JPEG storage
    - Gemini 3.8 Flash
    - Gemini 3.6 Flash fallback
    - Automatic 503 retry
    - Automatic 429 retry
    - Exponential backoff
    - Local AI fallback
    - HTTP server
    - CORS
    - GPSAgri dashboard compatibility

HTTP ENDPOINTS:

    GET /capture
        Capture image, return JPEG immediately, analyze in background

    GET /image
        Returns latest captured JPEG

    GET /ai_result
        Returns processing state + latest AI JSON

    GET /status
        Returns ESP32-CAM status

============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include <ArduinoJson.h>

// ============================================================
// WIFI
// ============================================================

const char* WIFI_SSID     = "USERID";
const char* WIFI_PASSWORD = "11221122";

// ============================================================
// GEMINI API KEY
// ============================================================

const char* GEMINI_API_KEY =
    
    "**********";

// ============================================================
// GEMINI MODELS
// ============================================================

// Primary model
const char* GEMINI_PRIMARY_MODEL =
    "gemini-3.8-flash";

// Secondary model used if primary model
// remains unavailable after retries.
const char* GEMINI_FALLBACK_MODEL =
    "gemini-3.6-flash";

// ============================================================
// GEMINI SERVER
// ============================================================

const char* GEMINI_HOST =
    "generativelanguage.googleapis.com";

// ============================================================
// HTTP SERVER
// ============================================================

WebServer server(80);

// ============================================================
// AI THINKER ESP32-CAM PINS
// ============================================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0

#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============================================================
// CAMERA SETTINGS
// ============================================================

#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA

// 10 = good JPEG quality
// Lower number = higher image quality / larger file
#define CAMERA_JPEG_QUALITY 10

// ============================================================
// GEMINI RETRY SETTINGS
// ============================================================

#define PRIMARY_MAX_RETRIES 4
#define FALLBACK_MAX_RETRIES 2

// 2s, 4s, 8s, 16s
#define GEMINI_RETRY_BASE_MS 2000

// ============================================================
// IMAGE STORAGE
// ============================================================

uint8_t* latestImage = nullptr;

size_t latestImageSize = 0;

// ============================================================
// STATUS
// ============================================================

bool cameraReady = false;
volatile bool geminiBusy = false;
bool aiAvailable = false;

unsigned long lastCaptureTime = 0;

// ============================================================
// AI RESULT
// ============================================================

String aiPlantName =
    "Unknown";

String aiCondition =
    "Unknown";

String aiDisease =
    "Unknown";

String aiDetails =
    "AI analysis not available.";

float aiConfidence = 0.0;
float aiHealthy = 0.0;
float aiAffected = 0.0;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

bool initCamera();

void connectWiFi();

void setupRoutes();

void handleRoot();

void handleCapture();

void handleImage();

void handleAIResult();

void handleStatus();

bool captureLatestImage();

void freeLatestImage();

bool analyzeImageWithGemini();

void geminiAnalysisTask(void* parameter);

bool tryGeminiModel(
    const char* model,
    int maxRetries
);

bool sendGeminiRequest(
    const char* model,
    const uint8_t* imageData,
    size_t imageSize,
    String& responseText,
    int& httpCode
);

String readChunkedResponse(
    WiFiClientSecure& client
);

bool parseGeminiResponse(
    const String& responseText
);

String extractJSONFromText(
    const String& text
);

String jsonEscape(
    const String& input
);

void setFallbackAIResult(
    const String& reason
);

void printAIResult();

void sendCORS();

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1500);

    Serial.println();
    Serial.println();
    Serial.println("================================================");
    Serial.println("       GPSAGRI ESP32-CAM STARTING");
    Serial.println("================================================");

    // --------------------------------------------------------
    // CAMERA
    // --------------------------------------------------------

    Serial.println();
    Serial.println("Initializing camera...");

    if (initCamera())
    {
        cameraReady = true;

        Serial.println(
            "Camera initialized successfully."
        );
    }
    else
    {
        cameraReady = false;

        Serial.println(
            "ERROR: Camera initialization failed."
        );
    }

    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    connectWiFi();

    // --------------------------------------------------------
    // HTTP SERVER
    // --------------------------------------------------------

    setupRoutes();

    server.begin();

    Serial.println();
    Serial.println("================================================");
    Serial.println("             HTTP SERVER READY");
    Serial.println("================================================");

    Serial.print("ESP32-CAM IP: ");
    Serial.println(
        WiFi.localIP()
    );

    Serial.println();
    Serial.println("Endpoints:");
    Serial.println("  /capture");
    Serial.println("  /image");
    Serial.println("  /ai_result");
    Serial.println("  /status");

    Serial.println();
    Serial.println("GPSAgri ESP32-CAM READY.");
    Serial.println("================================================");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    server.handleClient();

    // --------------------------------------------------------
    // WIFI RECONNECT
    // --------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        static unsigned long lastReconnect =
            0;

        if (
            millis() - lastReconnect > 10000
        )
        {
            lastReconnect = millis();

            Serial.println();
            Serial.println(
                "WiFi disconnected."
            );

            Serial.println(
                "Trying WiFi reconnect..."
            );

            WiFi.disconnect();

            delay(100);

            WiFi.begin(
                WIFI_SSID,
                WIFI_PASSWORD
            );
        }
    }
}

// ============================================================
// CAMERA INITIALIZATION
// ============================================================

bool initCamera()
{
    camera_config_t config;

    config.ledc_channel =
        LEDC_CHANNEL_0;

    config.ledc_timer =
        LEDC_TIMER_0;

    config.pin_d0 =
        Y2_GPIO_NUM;

    config.pin_d1 =
        Y3_GPIO_NUM;

    config.pin_d2 =
        Y4_GPIO_NUM;

    config.pin_d3 =
        Y5_GPIO_NUM;

    config.pin_d4 =
        Y6_GPIO_NUM;

    config.pin_d5 =
        Y7_GPIO_NUM;

    config.pin_d6 =
        Y8_GPIO_NUM;

    config.pin_d7 =
        Y9_GPIO_NUM;

    config.pin_xclk =
        XCLK_GPIO_NUM;

    config.pin_pclk =
        PCLK_GPIO_NUM;

    config.pin_vsync =
        VSYNC_GPIO_NUM;

    config.pin_href =
        HREF_GPIO_NUM;

    config.pin_sccb_sda =
        SIOD_GPIO_NUM;

    config.pin_sccb_scl =
        SIOC_GPIO_NUM;

    config.pin_pwdn =
        PWDN_GPIO_NUM;

    config.pin_reset =
        RESET_GPIO_NUM;

    config.xclk_freq_hz =
        20000000;

    config.pixel_format =
        PIXFORMAT_JPEG;

    // --------------------------------------------------------
    // PSRAM
    // --------------------------------------------------------

  if (psramFound())
  {
    Serial.println("PSRAM detected.");

    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 5;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }
  else
  {
    Serial.println("PSRAM NOT detected.");

    config.frame_size = FRAMESIZE_VGA;    // 640 x 480
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

    // --------------------------------------------------------
    // CAMERA INIT
    // --------------------------------------------------------

    esp_err_t err =
        esp_camera_init(
            &config
        );

    if (
        err != ESP_OK
    )
    {
        Serial.print(
            "Camera init failed. Error: 0x"
        );

        Serial.println(
            err,
            HEX
        );

        return false;
    }

    // --------------------------------------------------------
    // BASIC SENSOR SETTINGS
    // --------------------------------------------------------

    sensor_t* sensor =
        esp_camera_sensor_get();

    if (sensor != nullptr)
    {
        sensor->set_brightness(
            sensor,
            0
        );

        sensor->set_contrast(
            sensor,
            0
        );

        sensor->set_saturation(
            sensor,
            0
        );

        sensor->set_special_effect(
            sensor,
            0
        );

        sensor->set_whitebal(
            sensor,
            1
        );

        sensor->set_awb_gain(
            sensor,
            1
        );

        sensor->set_wb_mode(
            sensor,
            0
        );

        sensor->set_exposure_ctrl(
            sensor,
            1
        );

        sensor->set_gain_ctrl(
            sensor,
            1
        );

        sensor->set_bpc(
            sensor,
            0
        );

        sensor->set_wpc(
            sensor,
            1
        );

        sensor->set_raw_gma(
            sensor,
            1
        );

        sensor->set_lenc(
            sensor,
            1
        );
    }

    Serial.print(
        "JPEG quality: "
    );

    Serial.println(
        CAMERA_JPEG_QUALITY
    );

    return true;
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi()
{
    Serial.println();
    Serial.println(
        "Connecting to WiFi..."
    );

    WiFi.mode(
        WIFI_STA
    );

    // Helps maintain connection
    // during camera/network activity.
    WiFi.setSleep(false);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    unsigned long start =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 30000
    )
    {
        delay(500);

        Serial.print(".");
    }

    Serial.println();

    if (
        WiFi.status() == WL_CONNECTED
    )
    {
        Serial.println();
        Serial.println(
            "WiFi connected."
        );

        Serial.print(
            "IP address: "
        );

        Serial.println(
            WiFi.localIP()
        );

        Serial.print(
            "RSSI: "
        );

        Serial.println(
            WiFi.RSSI()
        );
    }
    else
    {
        Serial.println();
        Serial.println(
            "WiFi connection failed."
        );
    }
}

// ============================================================
// HTTP ROUTES
// ============================================================

void setupRoutes()
{
    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    server.on(
        "/capture",
        HTTP_GET,
        handleCapture
    );

    server.on(
        "/image",
        HTTP_GET,
        handleImage
    );

    server.on(
        "/ai_result",
        HTTP_GET,
        handleAIResult
    );

    server.on(
        "/status",
        HTTP_GET,
        handleStatus
    );

    server.on(
        "/favicon.ico",
        HTTP_GET,
        []()
        {
            sendCORS();

            server.send(
                204,
                "text/plain",
                ""
            );
        }
    );
}

// ============================================================
// ROOT
// ============================================================

void handleRoot()
{
    sendCORS();

    String html;

    html +=
        "<!DOCTYPE html>";

    html +=
        "<html>";

    html +=
        "<head>";

    html +=
        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'>";

    html +=
        "<title>GPSAgri ESP32-CAM</title>";

    html +=
        "</head>";

    html +=
        "<body>";

    html +=
        "<h1>GPSAgri ESP32-CAM</h1>";

    html +=
        "<p>IP: ";

    html +=
        WiFi.localIP().toString();

    html +=
        "</p>";

    html +=
        "<p>Camera: ";

    html +=
        cameraReady ?
        "READY" :
        "ERROR";

    html +=
        "</p>";

    html +=
        "<p><a href='/capture'>Capture</a></p>";

    html +=
        "<p><a href='/image'>Latest Image</a></p>";

    html +=
        "<p><a href='/ai_result'>AI Result</a></p>";

    html +=
        "<p><a href='/status'>Status</a></p>";

    html +=
        "</body>";

    html +=
        "</html>";

    server.send(
        200,
        "text/html",
        html
    );
}

// ============================================================
// CAPTURE
// ============================================================

void handleCapture()
{
    Serial.println();
    Serial.println("================================================");
    Serial.println("CAPTURE REQUEST RECEIVED");
    Serial.println("================================================");

    if (!cameraReady)
    {
        sendCORS();
        server.send(500, "application/json",
                    "{\"success\":false,\"error\":\"Camera not ready\"}");
        return;
    }

    // One capture/analysis at a time: the stored JPEG must remain unchanged
    // while the background Gemini task reads it.
    if (geminiBusy)
    {
        sendCORS();
        server.send(429, "application/json",
                    "{\"success\":false,\"processing\":true,"
                    "\"error\":\"AI analysis already running. Please wait.\"}");
        return;
    }

    if (!captureLatestImage())
    {
        sendCORS();
        server.send(500, "application/json",
                    "{\"success\":false,\"error\":\"Camera capture failed\"}");
        return;
    }

    lastCaptureTime = millis();

    Serial.print("JPEG captured: ");
    Serial.print(latestImageSize);
    Serial.println(" bytes");

    // Reset the previous result and mark the new analysis as pending.
    aiAvailable = false;
    aiPlantName = "";
    aiCondition = "";
    aiDisease = "";
    aiDetails = "Gemini analysis is processing this image.";
    aiConfidence = 0.0;
    aiHealthy = 0.0;
    aiAffected = 0.0;
    geminiBusy = true;

    // Start Gemini in a separate FreeRTOS task. The HTTP handler can now
    // send the captured JPEG without waiting for Google's response.
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        geminiAnalysisTask,
        "GeminiAnalysis",
        16384,
        nullptr,
        1,
        nullptr,
        1
    );

    if (taskCreated != pdPASS)
    {
        geminiBusy = false;
        setFallbackAIResult("Could not start the background Gemini task. Check available RAM.");
        Serial.println("ERROR: Could not create Gemini task.");
    }

    // Return the JPEG immediately to the dashboard.
    sendCORS();

    if (latestImage != nullptr && latestImageSize > 0)
    {
        server.setContentLength(latestImageSize);
        server.send(200, "image/jpeg", "");

        WiFiClient client = server.client();
        size_t sent = 0;

        while (sent < latestImageSize)
        {
            size_t chunk = latestImageSize - sent;
            if (chunk > 1024) chunk = 1024;

            size_t written = client.write(latestImage + sent, chunk);
            if (written == 0) break;

            sent += written;
            delay(1);
        }

        Serial.print("JPEG sent to dashboard: ");
        Serial.print(sent);
        Serial.println(" bytes");
    }
    else
    {
        // Normally unreachable after a successful capture.
        Serial.println("ERROR: Captured image buffer is empty.");
    }

    Serial.println("Image response completed; Gemini continues in background.");
    Serial.println("================================================");
}

// ============================================================
// CAPTURE LATEST IMAGE
// ============================================================

bool captureLatestImage()
{
    Serial.println();
    Serial.println(
        "Capturing camera frame..."
    );

    camera_fb_t* fb =
        esp_camera_fb_get();

    if (
        fb == nullptr
    )
    {
        Serial.println(
            "ERROR: Camera frame capture failed."
        );

        return false;
    }

    Serial.print(
        "Captured frame length: "
    );

    Serial.println(
        fb->len
    );

    // --------------------------------------------------------
    // Allocate PSRAM
    // --------------------------------------------------------

    uint8_t* newBuffer =
        nullptr;

    if (
        psramFound()
    )
    {
        newBuffer =
            (uint8_t*)ps_malloc(
                fb->len
            );
    }

    // --------------------------------------------------------
    // Normal heap fallback
    // --------------------------------------------------------

    if (
        newBuffer == nullptr
    )
    {
        Serial.println(
            "PSRAM allocation failed."
        );

        Serial.println(
            "Trying normal heap..."
        );

        newBuffer =
            (uint8_t*)malloc(
                fb->len
            );
    }

    if (
        newBuffer == nullptr
    )
    {
        Serial.println(
            "ERROR: Image memory allocation failed."
        );

        esp_camera_fb_return(
            fb
        );

        return false;
    }

    // --------------------------------------------------------
    // Copy
    // --------------------------------------------------------

    memcpy(
        newBuffer,
        fb->buf,
        fb->len
    );

    size_t newSize =
        fb->len;

    // --------------------------------------------------------
    // Return camera frame
    // --------------------------------------------------------

    esp_camera_fb_return(
        fb
    );

    // --------------------------------------------------------
    // Replace previous image
    // --------------------------------------------------------

    freeLatestImage();

    latestImage =
        newBuffer;

    latestImageSize =
        newSize;

    Serial.print(
        "Latest image stored: "
    );

    Serial.print(
        latestImageSize
    );

    Serial.println(
        " bytes"
    );

    return true;
}

// ============================================================
// FREE IMAGE
// ============================================================

void freeLatestImage()
{
    if (
        latestImage != nullptr
    )
    {
        free(
            latestImage
        );

        latestImage =
            nullptr;
    }

    latestImageSize = 0;
}

// ============================================================
// IMAGE ENDPOINT
// ============================================================

void handleImage()
{
    sendCORS();

    if (
        latestImage == nullptr ||
        latestImageSize == 0
    )
    {
        server.send(
            404,
            "application/json",
            "{\"success\":false,"
            "\"error\":\"No image captured yet\"}"
        );

        return;
    }

    server.setContentLength(
        latestImageSize
    );

    server.send(
        200,
        "image/jpeg",
        ""
    );

    WiFiClient client =
        server.client();

    size_t sent = 0;

    while (
        sent < latestImageSize
    )
    {
        size_t remaining =
            latestImageSize -
            sent;

        size_t chunk =
            remaining;

        if (
            chunk > 1024
        )
        {
            chunk = 1024;
        }

        size_t written =
            client.write(
                latestImage + sent,
                chunk
            );

        if (
            written == 0
        )
        {
            break;
        }

        sent += written;

        delay(1);
    }
}

// ============================================================
// AI RESULT
// ============================================================

void handleAIResult()
{
    sendCORS();

    DynamicJsonDocument doc(2048);
    doc["success"] = true;
    doc["processing"] = geminiBusy;

    // Do not read result Strings while the background task is updating them.
    // The dashboard polls again once processing becomes false.
    if (geminiBusy)
    {
        doc["plant_name"] = "";
        doc["condition"] = "";
        doc["disease"] = "";
        doc["confidence"] = 0;
        doc["healthy"] = 0;
        doc["affected"] = 0;
        doc["ai_details"] = "Gemini is analyzing the captured image...";
        doc["ai_available"] = false;
    }
    else
    {
        doc["plant_name"] = aiPlantName;
        doc["condition"] = aiCondition;
        doc["disease"] = aiDisease;
        doc["confidence"] = aiConfidence;
        doc["healthy"] = aiHealthy;
        doc["affected"] = aiAffected;
        doc["ai_details"] = aiDetails;
        doc["ai_available"] = aiAvailable;
    }

    doc["image_available"] = (latestImage != nullptr && latestImageSize > 0);
    doc["image_size"] = latestImageSize;
    doc["timestamp"] = millis();

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

// ============================================================
// STATUS
// ============================================================

void handleStatus()
{
    sendCORS();

    DynamicJsonDocument doc(
        4096
    );

    doc["device"] =
        "GPSAgri ESP32-CAM";

    doc["camera"] =
        cameraReady;

    doc["wifi"] =
        (
            WiFi.status() ==
            WL_CONNECTED
        );

    doc["ip"] =
        WiFi.localIP().toString();

    doc["rssi"] =
        WiFi.RSSI();

    doc["psram"] =
        psramFound();

    doc["image_available"] =
        (
            latestImage != nullptr &&
            latestImageSize > 0
        );

    doc["image_size"] =
        latestImageSize;

    doc["gemini_busy"] =
        geminiBusy;

    doc["ai_available"] =
        aiAvailable;

    doc["primary_model"] =
        GEMINI_PRIMARY_MODEL;

    doc["fallback_model"] =
        GEMINI_FALLBACK_MODEL;

    String output;

    serializeJson(
        doc,
        output
    );

    server.send(
        200,
        "application/json",
        output
    );
}

// ============================================================
// BACKGROUND GEMINI TASK
// ============================================================

void geminiAnalysisTask(void* parameter)
{
    Serial.println();
    Serial.println("Background Gemini task started.");

    bool success = analyzeImageWithGemini();

    if (success)
    {
        Serial.println("Background Gemini analysis completed.");
    }
    else
    {
        Serial.println("Background Gemini analysis failed; fallback result retained.");
    }

    // Set this last: /ai_result will expose the completed result only after
    // the analysis function has finished updating the result fields.
    geminiBusy = false;
    Serial.println("Gemini task finished.");

    vTaskDelete(nullptr);
}

// ============================================================
// GEMINI ANALYSIS
// ============================================================

bool analyzeImageWithGemini()
{
    if (
        latestImage == nullptr ||
        latestImageSize == 0
    )
    {
        Serial.println(
            "No image available for Gemini."
        );

        setFallbackAIResult(
            "No captured image is available."
        );

        return false;
    }

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "GEMINI AI ANALYSIS"
    );

    Serial.println(
        "========================================"
    );

    // --------------------------------------------------------
    // PRIMARY MODEL
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "Trying primary Gemini model:"
    );

    Serial.println(
        GEMINI_PRIMARY_MODEL
    );

    bool success =
        tryGeminiModel(
            GEMINI_PRIMARY_MODEL,
            PRIMARY_MAX_RETRIES
        );

    if (success)
    {
        return true;
    }

    // --------------------------------------------------------
    // FALLBACK MODEL
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "PRIMARY MODEL FAILED"
    );

    Serial.println(
        "Trying fallback Gemini model:"
    );

    Serial.println(
        GEMINI_FALLBACK_MODEL
    );

    Serial.println(
        "========================================"
    );

    success =
        tryGeminiModel(
            GEMINI_FALLBACK_MODEL,
            FALLBACK_MAX_RETRIES
        );

    if (success)
    {
        return true;
    }

    // --------------------------------------------------------
    // LOCAL FALLBACK
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "All Gemini attempts failed."
    );

    setFallbackAIResult(
        "Gemini service is temporarily unavailable."
    );

    return false;
}

// ============================================================
// TRY ONE GEMINI MODEL
// ============================================================

bool tryGeminiModel(
    const char* model,
    int maxRetries
)
{
    for (
        int attempt = 1;
        attempt <= maxRetries;
        attempt++
    )
    {
        Serial.println();

        Serial.print(
            "Gemini model: "
        );

        Serial.println(
            model
        );

        Serial.print(
            "Attempt "
        );

        Serial.print(
            attempt
        );

        Serial.print(
            "/"
        );

        Serial.println(
            maxRetries
        );

        String responseText;

        int httpCode = 0;

        bool requestOK =
            sendGeminiRequest(
                model,
                latestImage,
                latestImageSize,
                responseText,
                httpCode
            );

        // ----------------------------------------------------
        // SUCCESS
        // ----------------------------------------------------

        if (
            requestOK &&
            httpCode == 200
        )
        {
            Serial.println();
            Serial.println(
                "Gemini HTTP 200."
            );

            if (
                parseGeminiResponse(
                    responseText
                )
            )
            {
                aiAvailable =
                    true;

                printAIResult();

                return true;
            }

            Serial.println(
                "Gemini response JSON could not be parsed."
            );

            return false;
        }

        // ----------------------------------------------------
        // RETRY 503
        // ----------------------------------------------------

        if (
            httpCode == 503
        )
        {
            Serial.println();
            Serial.println(
                "Gemini HTTP 503."
            );

            Serial.println(
                "Service temporarily unavailable."
            );

            if (
                attempt < maxRetries
            )
            {
                unsigned long delayTime =
                    GEMINI_RETRY_BASE_MS *
                    (1UL << (attempt - 1));

                // Small jitter
                unsigned long jitter =
                    250 +
                    (millis() % 750);

                delayTime +=
                    jitter;

                Serial.print(
                    "Retrying in "
                );

                Serial.print(
                    delayTime
                );

                Serial.println(
                    " ms..."
                );

                delay(
                    delayTime
                );

                continue;
            }

            Serial.println(
                "503 retry limit reached."
            );

            return false;
        }

        // ----------------------------------------------------
        // RETRY 429
        // ----------------------------------------------------

        if (
            httpCode == 429
        )
        {
            Serial.println();
            Serial.println(
                "Gemini HTTP 429."
            );

            Serial.println(
                "Rate limit/quota response."
            );

            if (
                attempt < maxRetries
            )
            {
                unsigned long delayTime =
                    GEMINI_RETRY_BASE_MS *
                    (1UL << (attempt - 1));

                delayTime +=
                    500;

                Serial.print(
                    "Retrying in "
                );

                Serial.print(
                    delayTime
                );

                Serial.println(
                    " ms..."
                );

                delay(
                    delayTime
                );

                continue;
            }

            return false;
        }

        // ----------------------------------------------------
        // RETRY 500
        // ----------------------------------------------------

        if (
            httpCode == 500
        )
        {
            Serial.println(
                "Gemini HTTP 500."
            );

            if (
                attempt < maxRetries
            )
            {
                unsigned long delayTime =
                    GEMINI_RETRY_BASE_MS *
                    (1UL << (attempt - 1));

                delay(
                    delayTime
                );

                continue;
            }

            return false;
        }

        // ----------------------------------------------------
        // 504
        // ----------------------------------------------------

        if (
            httpCode == 504
        )
        {
            Serial.println(
                "Gemini HTTP 504 timeout."
            );

            if (
                attempt < maxRetries
            )
            {
                unsigned long delayTime =
                    GEMINI_RETRY_BASE_MS *
                    (1UL << (attempt - 1));

                delay(
                    delayTime
                );

                continue;
            }

            return false;
        }

        // ----------------------------------------------------
        // Other errors
        // ----------------------------------------------------

        Serial.print(
            "Non-retryable Gemini HTTP error: "
        );

        Serial.println(
            httpCode
        );

        return false;
    }

    return false;
}

// ============================================================
// SEND GEMINI REQUEST
// ============================================================

bool sendGeminiRequest(
    const char* model,
    const uint8_t* imageData,
    size_t imageSize,
    String& responseText,
    int& httpCode
)
{
    responseText = "";

    httpCode = 0;

    if (
        imageData == nullptr ||
        imageSize == 0
    )
    {
        return false;
    }

    Serial.print(
        "JPEG size: "
    );

    Serial.println(
        imageSize
    );

    // --------------------------------------------------------
    // BASE64 SIZE
    // --------------------------------------------------------

    size_t base64Size =
        ((imageSize + 2) / 3) * 4;

    Serial.print(
        "Base64 size: "
    );

    Serial.println(
        base64Size
    );

    // --------------------------------------------------------
    // PROMPT
    // --------------------------------------------------------

    String prompt =
        "Analyze this agriculture plant image. "
        "Identify the plant if possible. "
        "Describe its visible condition. "
        "Identify visible disease if present, "
        "otherwise say None observed. "
        "Estimate healthy and affected percentages. "
        "Give confidence from 0 to 100. "
        "Return ONLY valid JSON. "
        "Do not use markdown or code fences. "
        "Use exactly these fields: "
        "plant_name, condition, disease, confidence, "
        "healthy, affected, ai_details. "
        "The healthy and affected percentages should "
        "approximately total 100.";

    String escapedPrompt =
        jsonEscape(
            prompt
        );

    // --------------------------------------------------------
    // JSON PREFIX
    // --------------------------------------------------------

    String prefix =
        "{\"contents\":["
        "{\"parts\":["
        "{\"text\":\"";

    prefix +=
        escapedPrompt;

    prefix +=
        "\"},"
        "{\"inline_data\":"
        "{\"mime_type\":\"image/jpeg\","
        "\"data\":\"";

    // --------------------------------------------------------
    // JSON SUFFIX
    // --------------------------------------------------------

    String suffix =
        "\"}}]}]}";

    size_t bodyLength =
        prefix.length() +
        base64Size +
        suffix.length();

    Serial.print(
        "HTTP body size: "
    );

    Serial.println(
        bodyLength
    );

    // --------------------------------------------------------
    // TLS CLIENT
    // --------------------------------------------------------

    WiFiClientSecure client;

    // Prototype mode.
    // For production, use certificate validation.
    client.setInsecure();

    client.setTimeout(
        60
    );

    Serial.println(
        "Connecting to Gemini..."
    );

    if (
        !client.connect(
            GEMINI_HOST,
            443
        )
    )
    {
        Serial.println(
            "Gemini TLS connection failed."
        );

        return false;
    }

    Serial.println(
        "Gemini TLS connected."
    );

    // --------------------------------------------------------
    // URL
    // --------------------------------------------------------

    String url =
        "/v1beta/models/";

    url +=
        model;

    url +=
        ":generateContent";

    // --------------------------------------------------------
    // HTTP REQUEST
    // --------------------------------------------------------

    client.print(
        "POST "
    );

    client.print(
        url
    );

    client.println(
        " HTTP/1.1"
    );

    client.print(
        "Host: "
    );

    client.println(
        GEMINI_HOST
    );

    // Current documented API-key header
    client.print(
        "x-goog-api-key: "
    );

    client.println(
        GEMINI_API_KEY
    );

    client.println(
        "Content-Type: application/json"
    );

    client.print(
        "Content-Length: "
    );

    client.println(
        bodyLength
    );

    client.println(
        "Connection: close"
    );

    client.println();

    // --------------------------------------------------------
    // SEND JSON PREFIX
    // --------------------------------------------------------

    client.print(
        prefix
    );

    Serial.println(
        "Streaming image Base64 to Gemini..."
    );

    // --------------------------------------------------------
    // BASE64 TABLE
    // --------------------------------------------------------

    const char base64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    // --------------------------------------------------------
    // BASE64 STREAM
    // --------------------------------------------------------

    size_t i = 0;

    while (
        i + 2 < imageSize
    )
    {
        uint32_t value =
            (
                ((uint32_t)imageData[i])
                << 16
            )
            |
            (
                ((uint32_t)imageData[i + 1])
                << 8
            )
            |
            (
                (uint32_t)imageData[i + 2]
            );

        char encoded[4];

        encoded[0] =
            base64Table[
                (value >> 18) & 0x3F
            ];

        encoded[1] =
            base64Table[
                (value >> 12) & 0x3F
            ];

        encoded[2] =
            base64Table[
                (value >> 6) & 0x3F
            ];

        encoded[3] =
            base64Table[
                value & 0x3F
            ];

        client.write(
            (const uint8_t*)encoded,
            4
        );

        i += 3;

        if (
            (i % 3072) == 0
        )
        {
            delay(1);
        }
    }

    // --------------------------------------------------------
    // LAST 1-2 BYTES
    // --------------------------------------------------------

    if (
        i < imageSize
    )
    {
        uint32_t value =
            (
                (uint32_t)imageData[i]
                << 16
            );

        if (
            i + 1 < imageSize
        )
        {
            value |=
                (
                    (uint32_t)imageData[i + 1]
                    << 8
                );
        }

        char encoded[4];

        encoded[0] =
            base64Table[
                (value >> 18) & 0x3F
            ];

        encoded[1] =
            base64Table[
                (value >> 12) & 0x3F
            ];

        if (
            i + 1 < imageSize
        )
        {
            encoded[2] =
                base64Table[
                    (value >> 6) & 0x3F
                ];
        }
        else
        {
            encoded[2] = '=';
        }

        encoded[3] = '=';

        client.write(
            (const uint8_t*)encoded,
            4
        );
    }

    // --------------------------------------------------------
    // END JSON
    // --------------------------------------------------------

    client.print(
        suffix
    );

    Serial.println(
        "Gemini request sent."
    );

    // --------------------------------------------------------
    // WAIT RESPONSE
    // --------------------------------------------------------

    unsigned long waitStart =
        millis();

    while (
        !client.available() &&
        millis() - waitStart < 65000
    )
    {
        delay(50);
    }

    if (
        !client.available()
    )
    {
        Serial.println(
            "Gemini response timeout."
        );

        client.stop();

        return false;
    }

    // --------------------------------------------------------
    // STATUS LINE
    // --------------------------------------------------------

    String statusLine =
        client.readStringUntil(
            '\n'
        );

    statusLine.trim();

    Serial.print(
        "HTTP status: "
    );

    Serial.println(
        statusLine
    );

    // --------------------------------------------------------
    // HTTP CODE
    // --------------------------------------------------------

    int firstSpace =
        statusLine.indexOf(
            ' '
        );

    if (
        firstSpace >= 0 &&
        statusLine.length() >=
        firstSpace + 4
    )
    {
        httpCode =
            statusLine.substring(
                firstSpace + 1,
                firstSpace + 4
            ).toInt();
    }

    // --------------------------------------------------------
    // HEADERS
    // --------------------------------------------------------

    bool chunked = false;

    int contentLength = -1;

    while (
        client.connected()
    )
    {
        String header =
            client.readStringUntil(
                '\n'
            );

        header.trim();

        if (
            header.length() == 0
        )
        {
            break;
        }

        String lower =
            header;

        lower.toLowerCase();

        if (
            lower.indexOf(
                "transfer-encoding: chunked"
            ) >= 0
        )
        {
            chunked = true;
        }

        if (
            lower.startsWith(
                "content-length:"
            )
        )
        {
            int colon =
                header.indexOf(
                    ':'
                );

            if (
                colon >= 0
            )
            {
                contentLength =
                    header.substring(
                        colon + 1
                    ).toInt();
            }
        }
    }

    // --------------------------------------------------------
    // READ BODY
    // --------------------------------------------------------

    if (
        chunked
    )
    {
        responseText =
            readChunkedResponse(
                client
            );
    }
    else
    {
        if (
            contentLength >= 0
        )
        {
            responseText.reserve(
                contentLength
            );

            while (
                responseText.length() <
                (size_t)contentLength
            )
            {
                while (
                    client.available()
                )
                {
                    responseText +=
                        (char)client.read();

                    if (
                        responseText.length() >=
                        (size_t)contentLength
                    )
                    {
                        break;
                    }
                }

                if (
                    !client.connected() &&
                    !client.available()
                )
                {
                    break;
                }

                delay(1);
            }
        }
        else
        {
            while (
                client.connected() ||
                client.available()
            )
            {
                while (
                    client.available()
                )
                {
                    responseText +=
                        (char)client.read();
                }

                delay(1);
            }
        }
    }

    client.stop();

    // --------------------------------------------------------
    // ERROR RESPONSE
    // --------------------------------------------------------

    if (
        httpCode != 200
    )
    {
        Serial.println();
        Serial.println(
            "Gemini returned an error."
        );

        Serial.println(
            "Gemini error response:"
        );

        Serial.println(
            responseText
        );

        return false;
    }

    return true;
}

// ============================================================
// CHUNKED RESPONSE
// ============================================================

String readChunkedResponse(
    WiFiClientSecure& client
)
{
    String result;

    while (true)
    {
        String sizeLine =
            client.readStringUntil(
                '\n'
            );

        sizeLine.trim();

        if (
            sizeLine.length() == 0
        )
        {
            continue;
        }

        int chunkSize =
            strtol(
                sizeLine.c_str(),
                nullptr,
                16
            );

        if (
            chunkSize <= 0
        )
        {
            // Consume final CR/LF if present.
            while (
                client.available()
            )
            {
                char c =
                    client.read();

                if (
                    c == '\n'
                )
                {
                    break;
                }
            }

            break;
        }

        int received = 0;

        while (
            received < chunkSize
        )
        {
            while (
                client.available() &&
                received < chunkSize
            )
            {
                result +=
                    (char)client.read();

                received++;
            }

            if (
                received < chunkSize
            )
            {
                delay(1);
            }
        }

        // CR
        if (
            client.available()
        )
        {
            client.read();
        }

        // LF
        if (
            client.available()
        )
        {
            client.read();
        }
    }

    return result;
}

// ============================================================
// PARSE GEMINI RESPONSE
// ============================================================

bool parseGeminiResponse(
    const String& responseText
)
{
    Serial.println();
    Serial.println(
        "Parsing Gemini response..."
    );

    DynamicJsonDocument responseDoc(
        12288
    );

    DeserializationError error =
        deserializeJson(
            responseDoc,
            responseText
        );

    if (
        error
    )
    {
        Serial.print(
            "Gemini response parse error: "
        );

        Serial.println(
            error.c_str()
        );

        Serial.println(
            "Raw Gemini response:"
        );

        Serial.println(
            responseText
        );

        return false;
    }

    // --------------------------------------------------------
    // Gemini response path
    // --------------------------------------------------------

    JsonVariant textVariant =
        responseDoc[
            "candidates"
        ][0][
            "content"
        ][
            "parts"
        ][0][
            "text"
        ];

    if (
        textVariant.isNull()
    )
    {
        Serial.println(
            "No Gemini text found."
        );

        return false;
    }

    String aiText =
        textVariant.as<String>();

    Serial.println();
    Serial.println(
        "Gemini raw AI text:"
    );

    Serial.println(
        aiText
    );

    // --------------------------------------------------------
    // Extract JSON
    // --------------------------------------------------------

    String jsonText =
        extractJSONFromText(
            aiText
        );

    if (
        jsonText.length() == 0
    )
    {
        Serial.println(
            "Could not find JSON object."
        );

        return false;
    }

    Serial.println();
    Serial.println(
        "Extracted AI JSON:"
    );

    Serial.println(
        jsonText
    );

    // --------------------------------------------------------
    // Parse AI JSON
    // --------------------------------------------------------

    DynamicJsonDocument aiDoc(
        8192
    );

    error =
        deserializeJson(
            aiDoc,
            jsonText
        );

    if (
        error
    )
    {
        Serial.print(
            "AI JSON parse failed: "
        );

        Serial.println(
            error.c_str()
        );

        return false;
    }

    // --------------------------------------------------------
    // Read strings
    // --------------------------------------------------------

    aiPlantName =
        aiDoc[
            "plant_name"
        ] |
        "Unknown";

    aiCondition =
        aiDoc[
            "condition"
        ] |
        "Unknown";

    aiDisease =
        aiDoc[
            "disease"
        ] |
        "None observed";

    aiDetails =
        aiDoc[
            "ai_details"
        ] |
        "No additional details.";

    // --------------------------------------------------------
    // Read numbers
    // --------------------------------------------------------

    aiConfidence =
        aiDoc[
            "confidence"
        ] |
        0.0;

    aiHealthy =
        aiDoc[
            "healthy"
        ] |
        0.0;

    aiAffected =
        aiDoc[
            "affected"
        ] |
        0.0;

    // --------------------------------------------------------
    // Clamp values
    // --------------------------------------------------------

    aiConfidence =
        constrain(
            aiConfidence,
            0.0,
            100.0
        );

    aiHealthy =
        constrain(
            aiHealthy,
            0.0,
            100.0
        );

    aiAffected =
        constrain(
            aiAffected,
            0.0,
            100.0
        );

    aiAvailable =
        true;

    return true;
}

// ============================================================
// EXTRACT JSON
// ============================================================

String extractJSONFromText(
    const String& text
)
{
    String cleaned =
        text;

    cleaned.trim();

    // --------------------------------------------------------
    // Remove markdown code fence
    // --------------------------------------------------------

    if (
        cleaned.startsWith(
            "```"
        )
    )
    {
        int firstNewLine =
            cleaned.indexOf(
                '\n'
            );

        if (
            firstNewLine >= 0
        )
        {
            cleaned =
                cleaned.substring(
                    firstNewLine + 1
                );
        }

        int lastFence =
            cleaned.lastIndexOf(
                "```"
            );

        if (
            lastFence >= 0
        )
        {
            cleaned =
                cleaned.substring(
                    0,
                    lastFence
                );
        }

        cleaned.trim();
    }

    // --------------------------------------------------------
    // Find first {
    // --------------------------------------------------------

    int start =
        cleaned.indexOf(
            '{'
        );

    // --------------------------------------------------------
    // Find last }
    // --------------------------------------------------------

    int end =
        cleaned.lastIndexOf(
            '}'
        );

    if (
        start < 0 ||
        end < 0 ||
        end <= start
    )
    {
        return "";
    }

    return cleaned.substring(
        start,
        end + 1
    );
}

// ============================================================
// FALLBACK RESULT
// ============================================================

void setFallbackAIResult(
    const String& reason
)
{
    aiAvailable =
        false;

    aiPlantName =
        "AI unavailable";

    aiCondition =
        "Image captured";

    aiDisease =
        "Analysis unavailable";

    aiConfidence =
        0.0;

    aiHealthy =
        0.0;

    aiAffected =
        0.0;

    aiDetails =
        "The ESP32-CAM captured the image successfully, "
        "but Gemini analysis is temporarily unavailable. "
        "Reason: " +
        reason;

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "LOCAL FALLBACK RESULT"
    );

    Serial.println(
        "========================================"
    );

    Serial.println(
        aiDetails
    );

    Serial.println(
        "========================================"
    );
}

// ============================================================
// PRINT AI RESULT
// ============================================================

void printAIResult()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "GEMINI AI RESULT"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Plant: "
    );

    Serial.println(
        aiPlantName
    );

    Serial.print(
        "Condition: "
    );

    Serial.println(
        aiCondition
    );

    Serial.print(
        "Disease: "
    );

    Serial.println(
        aiDisease
    );

    Serial.print(
        "Healthy: "
    );

    Serial.print(
        aiHealthy
    );

    Serial.println(
        "%"
    );

    Serial.print(
        "Affected: "
    );

    Serial.print(
        aiAffected
    );

    Serial.println(
        "%"
    );

    Serial.print(
        "Confidence: "
    );

    Serial.print(
        aiConfidence
    );

    Serial.println(
        "%"
    );

    Serial.print(
        "Details: "
    );

    Serial.println(
        aiDetails
    );

    Serial.println(
        "========================================"
    );
}

// ============================================================
// JSON ESCAPE
// ============================================================

String jsonEscape(
    const String& input
)
{
    String output;

    output.reserve(
        input.length() + 32
    );

    for (
        size_t i = 0;
        i < input.length();
        i++
    )
    {
        char c =
            input[i];

        switch (c)
        {
            case '\"':
                output +=
                    "\\\"";
                break;

            case '\\':
                output +=
                    "\\\\";
                break;

            case '\n':
                output +=
                    "\\n";
                break;

            case '\r':
                output +=
                    "\\r";
                break;

            case '\t':
                output +=
                    "\\t";
                break;

            default:
                output +=
                    c;
                break;
        }
    }

    return output;
}

// ============================================================
// CORS
// ============================================================

void sendCORS()
{
    server.sendHeader(
        "Access-Control-Allow-Origin",
        "*"
    );

    server.sendHeader(
        "Access-Control-Allow-Methods",
        "GET, OPTIONS"
    );

    server.sendHeader(
        "Access-Control-Allow-Headers",
        "*"
    );

    server.sendHeader(
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    server.sendHeader(
        "Pragma",
        "no-cache"
    );
}