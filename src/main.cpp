#include "Arduino.h"
#include "esp_camera.h"
#include "SD_MMC.h"
#include "Firebase_ESP_Client.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include <EEPROM.h>
#include "time.h"
#include "addons/TokenHelper.h"

// Cấu hình Firebase
#define API_KEY "AIzaSyCnNQE2m_QgCCFbN3-WbV932o7yjzwQlkc"
#define STORAGE_BUCKET_ID "captureimage-38a12.appspot.com"

// Cấu hình WiFi
#define WIFI_SSID "FEEE TDTU"
#define WIFI_PASSWORD "hoicochau"

// Khởi tạo Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Cấu hình camera pins cho ESP32-CAM
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// Biến đếm ảnh
int imageCounter = 0;
bool taskCompleted = false;

// Hàm khởi tạo WiFi
void initWiFi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  Serial.println("successfully connected to Wifi!!");
}

// Hàm khởi tạo Camera
void initCamera()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Cấu hình chất lượng ảnh
  config.frame_size = FRAMESIZE_CIF;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  Serial.println("Camera initialized successfully");
}

// Hàm khởi tạo thẻ SD
static esp_err_t initSDCard()
{
  if (!SD_MMC.begin("/sdcard", false)) // Đã thay đổi true thành false để không sử dụng 4-bit mode
  {
    Serial.println("SD Card Mount Failed");
    return ESP_FAIL;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("No SD Card attached");
    return ESP_FAIL;
  }

  Serial.print("SD_MMC Card Type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);

  // Kiểm tra filesystem
  File root = SD_MMC.open("/");
  if (!root)
  {
    Serial.println("Failed to open root directory");
    return ESP_FAIL;
  }
  if (!root.isDirectory())
  {
    Serial.println("Root is not a directory");
    return ESP_FAIL;
  }
  root.close();

  // Tạo thư mục images nếu chưa tồn tại
  if (!SD_MMC.exists("/images"))
  {
    Serial.println("Creating /images directory...");
    if (SD_MMC.mkdir("/images"))
    {
      Serial.println("Directory created");
    }
    else
    {
      Serial.println("Failed to create directory");
      return ESP_FAIL;
    }
  }

  Serial.println("SD Card initialized successfully");
  return ESP_OK;
}

// Hàm khởi tạo Firebase
void initFirebase()
{
  config.api_key = API_KEY;
  auth.user.email = "leminhquanlqd711@gmail.com";
  auth.user.password = "0355115950Lmq@";

  config.token_status_callback = tokenStatusCallback; // Gọi lại trạng thái token

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  delay(2000); // Thêm 2-3 giây delay để đợi quá trình xác thực hoàn tất
}

// Hàm chụp và lưu ảnh
void captureAndSaveImage()
{
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    return;
  }

  // Tạo tên file
  String fileName = "/images/picture" + String(imageCounter) + ".jpg";

  // Mount lại thẻ SD trước khi ghi
  if (!SD_MMC.begin("/sdcard", false))
  {
    Serial.println("SD Card not ready. Re-mounting...");
    if (!SD_MMC.begin("/sdcard", false))
    {
      Serial.println("Re-mount failed");
      esp_camera_fb_return(fb);
      return;
    }
  }

  // Lưu ảnh vào thẻ SD
  File file = SD_MMC.open(fileName.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }

  size_t bytesWritten = file.write(fb->buf, fb->len);
  if (bytesWritten != fb->len)
  {
    Serial.println("Failed to write full file");
    file.close();
    esp_camera_fb_return(fb);
    return;
  }

  file.close();
  Serial.printf("Saved file to path: %s\n", fileName.c_str());

  // Unmount SD card sau khi ghi file xong
  SD_MMC.end();

  // Mount lại thẻ SD để truy xuất file
  if (!SD_MMC.begin("/sdcard", false))
  {
    Serial.println("Failed to remount SD card");
    esp_camera_fb_return(fb);
    return;
  }

  // Upload lên Firebase
  if (Firebase.ready())
  {
    Serial.print("Uploading to Firebase Storage... ");
    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fileName.c_str(),
                                mem_storage_type_sd, fileName.c_str(), "image/jpeg"))
    {
      Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
    }
    else
    {
      Serial.printf("Upload failed: %s\n", fbdo.errorReason().c_str());
    }
  }
  else
  {
    Serial.println("Firebase not ready");
  }

  imageCounter++;
  esp_camera_fb_return(fb);
}

// Hàm setup
void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.println();

  // Khởi tạo và kiểm tra PSRAM
  if (!psramInit())
  {
    Serial.println("PSRAM initialization failed");
    return;
  }
  Serial.printf("\nPSRAM initialized. Size: %d bytes\n", ESP.getPsramSize());

  initWiFi();
  initCamera();

  if (initSDCard() != ESP_OK)
  {
    Serial.println("Failed to initialize SD card");
    return;
  }

  initFirebase();

  // Kiểm tra trạng thái Firebase
  if (!Firebase.ready())
  {
    Serial.println("Firebase not ready");
  }
  else
  {
    Serial.println("Firebase initialized successfully");
  }
}

// Hàm loop
void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    captureAndSaveImage();
  }
  else
  {
    Serial.println("WiFi is disconnected");
  }
  delay(5000); // Delay 5 giây giữa các lần chụp ảnh
}
