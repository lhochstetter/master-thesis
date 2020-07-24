#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "hwcrypto/aes.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include "led.h"
#include "button.h"
#include "wifi.h"

#include "ota.h"

extern const char *TAG;

static char ota_download_data[BUFFSIZE + 1] = { 0 };
static char ota_write_data[BUFFSIZE + 1] = { 0 };

static char m_ca_cert[1205];
static char m_https_key[1705];
static char m_https_crt[4492];
static char m_rsa_key[1676];
static size_t m_rsa_key_size = 0;

static void http_cleanup(esp_http_client_handle_t client)
{
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
}

static void __attribute__ ((noreturn)) task_fatal_error()
{
	ESP_LOGE(TAG, "Exiting task due to fatal error...");
	(void) vTaskDelete(NULL);

	while (1) {
		;
	}
}

static void meta_download(char *buffer, int *length)
{
	esp_err_t err;

	if (NULL == buffer)
		goto FAIL_FATAL_ERROR;

	if (NULL == length)
		goto FAIL_FATAL_ERROR;

	char *url = NULL;

	int url_size =
	    snprintf(NULL, 0, "%s%s", EXAMPLE_SERVER_URL, "meta");
	url = malloc(url_size + 1);
	sprintf(url, "%s%s", EXAMPLE_SERVER_URL, "meta");

	esp_http_client_config_t config = { 0 };
	config.cert_pem = (char *) m_ca_cert;
	config.client_cert_pem = m_https_crt;
	config.client_key_pem = m_https_key;
	config.url = url;

	ESP_LOGI(TAG, "downloading meta file @ %s", config.url);

	esp_http_client_handle_t client = esp_http_client_init(&config);
	if (client == NULL) {
		ESP_LOGE(TAG, "Failed to initialise HTTP connection");
		goto FAIL_CLEANUP_URL;
	}

	err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s",
			 esp_err_to_name(err));
		goto FAIL_CLEANUP_CLIENT;
	}

	esp_http_client_fetch_headers(client);

	/* download the meta file */
	int total_data_read = 0;
	while (1) {
		int data_read = esp_http_client_read(client, buffer,
						     BUFFSIZE);
		if (data_read < 0) {
			ESP_LOGE(TAG, "Error: SSL data read error");
			goto FAIL_CLEANUP_CONNECTION;
		} else if (data_read > 0) {
			if (err != ESP_OK) {
				goto FAIL_CLEANUP_CONNECTION;
			}
			total_data_read += data_read;
			ESP_LOGD(TAG, "Downloaded data length %d",
				 total_data_read);
		} else if (data_read == 0) {
			ESP_LOGI(TAG,
				 "Connection closed, meta file downloaded");
			break;
		}
	}

	*length = total_data_read;

	http_cleanup(client);
	free(url);
	url = NULL;

	return;

	/* intended fall throughs */
      FAIL_CLEANUP_CONNECTION:
	esp_http_client_close(client);

      FAIL_CLEANUP_CLIENT:
	esp_http_client_cleanup(client);

      FAIL_CLEANUP_URL:
	free(url);
	url = NULL;

      FAIL_FATAL_ERROR:
	task_fatal_error();
}

static void meta_decrypt(char *to_decrypt, int *length, char *decrypted)
{

	if (NULL == to_decrypt)
		goto FAIL_FATAL_ERROR;

	if (NULL == length)
		goto FAIL_FATAL_ERROR;

	if (NULL == decrypted)
		goto FAIL_FATAL_ERROR;

	size_t olen = 0;
	int ret = 0;
	mbedtls_pk_context pk;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	mbedtls_pk_init(&pk);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	mbedtls_entropy_init(&entropy);

	ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
				    &entropy, NULL, 0);

	if (0 != ret) {
		mbedtls_strerror(ret, decrypted, 1024);
		printf
		    (" failed\n  ! mbedtls_ctr_drbg_seed returned -0x%04x:%s\n",
		     -ret, decrypted);
		goto FAIL_CLEANUP_RSA;
	}
	ret =
	    mbedtls_pk_parse_key(&pk, (unsigned char *) m_rsa_key,
				 m_rsa_key_size, (unsigned char *) NULL,
				 0);

	if (0 != ret) {
		mbedtls_strerror(ret, decrypted, 1024);
		printf
		    (" failed\n  ! mbedtls_pk_parse_key returned -0x%04x:%s\n",
		     -ret, decrypted);
		goto FAIL_CLEANUP_RSA;
	}

	ret = mbedtls_pk_decrypt(&pk, (unsigned char *) to_decrypt,
				 *length,
				 (unsigned char *) decrypted, &olen,
				 1024, mbedtls_ctr_drbg_random, &ctr_drbg);

	if (0 != ret) {
		mbedtls_strerror(ret, decrypted, 1024);
		printf
		    (" failed\n  ! mbedtls_pk_decrypt returned -0x%04x: %s\n",
		     -ret, decrypted);
		goto FAIL_CLEANUP_RSA;
	}

	mbedtls_pk_free(&pk);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	return;

	/* intended fall through */
      FAIL_CLEANUP_RSA:
	mbedtls_pk_free(&pk);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);

      FAIL_FATAL_ERROR:
	task_fatal_error();
}


static void meta_parse(char *buffer, uint8_t aes_key[32],
		       uint8_t aes_iv[16], uint64_t * firmware_size)
{
	char *key = NULL;
	char *iv = NULL;
	char *size = NULL;

	size = strtok(ota_write_data, "\n");
	key = strtok(NULL, "\n");
	iv = strtok(NULL, "");

	strtok(size, "=");
	size = strtok(NULL, "");
	sscanf(size, "%llu", firmware_size);
	ESP_LOGI(TAG, "firmware size: %llu", *firmware_size);

	strtok(key, "=");
	key = strtok(NULL, "");
	ESP_LOGI(TAG, "aes key: %s", key);

	strtok(iv, "=");
	iv = strtok(NULL, "");
	ESP_LOGI(TAG, "aes iv:  %s\n", iv);

	char *iter = key;

	for (uint8_t i = 0; i < 32; i++) {
		sscanf(iter, "%2hhx", &(aes_key[i]));
		iter += 2;
	}

	iter = iv;

	for (uint8_t i = 0; i < 16; i++) {
		sscanf(iter, "%2hhx", &(aes_iv[i]));
		iter += 2;
	}
}

static bool is_firmware_version_newer(char *buffer,
				      const esp_partition_t * running)
{


	esp_app_desc_t new_app_info;

	// check current version with downloading
	memcpy(&new_app_info, &buffer[sizeof(esp_image_header_t)
				      +
				      sizeof
				      (esp_image_segment_header_t)],
	       sizeof(esp_app_desc_t));
	ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

	esp_app_desc_t running_app_info;
	if (esp_ota_get_partition_description
	    (running, &running_app_info) == ESP_OK) {
		ESP_LOGI(TAG, "Running firmware version: %s",
			 running_app_info.version);
	}

	const esp_partition_t
	    * last_invalid_app = esp_ota_get_last_invalid_partition();
	esp_app_desc_t invalid_app_info;
	if (esp_ota_get_partition_description
	    (last_invalid_app, &invalid_app_info) == ESP_OK) {
		ESP_LOGI(TAG, "Last invalid firmware version: %s",
			 invalid_app_info.version);
	}
	/* check current version with last invalid partition */
	if (last_invalid_app != NULL) {
		if (memcmp
		    (invalid_app_info.version,
		     new_app_info.version, sizeof(new_app_info.version))
		    == 0) {
			ESP_LOGI(TAG,
				 "New version is the same as invalid version.");
			ESP_LOGI(TAG,
				 "Previously, there was an attempt to launch the firmware with %s version, but it failed.",
				 invalid_app_info.version);
			ESP_LOGI(TAG,
				 "The firmware has been rolled back to the previous version.");

			xEventGroupClearBits(g_button_event_group, OTA_BIT);

			return false;
		}
	}

	/* parse major and minor versions */
	int running_major = 1;
	int running_minor = 1;

	sscanf(strtok(running_app_info.version, "."), "%d",
	       &running_major);
	sscanf(strtok(NULL, ""), "%d", &running_minor);

	int download_major = 0;
	int download_minor = 0;

	sscanf(strtok(new_app_info.version, "."), "%d", &download_major);
	sscanf(strtok(NULL, ""), "%d", &download_minor);

	/* check current version with download version */
	if ((running_major > download_major)
	    || (running_major == download_major
		&& running_minor >= download_minor)) {
		ESP_LOGI(TAG, "running: %d.%d, new: %d.%d", running_major,
			 running_minor, download_major, download_minor);
		ESP_LOGI(TAG, "no new update - reset update process");
		xEventGroupClearBits(g_button_event_group, OTA_BIT);
		return false;
	}

	return true;
}

static void load_certificates()
{

	nvs_handle handle;
	size_t size;

	nvs_open("config", NVS_READONLY, &handle);

	nvs_get_blob(handle, "CA_CERT", NULL, &size);
	nvs_get_blob(handle, "CA_CERT", m_ca_cert, &size);
	m_ca_cert[size] = '\0';

	nvs_get_blob(handle, "HTTPS_KEY", NULL, &size);
	nvs_get_blob(handle, "HTTPS_KEY", m_https_key, &size);
	m_https_key[size] = '\0';

	nvs_get_blob(handle, "HTTPS_CRT", NULL, &size);
	nvs_get_blob(handle, "HTTPS_CRT", m_https_crt, &size);
	m_https_crt[size] = '\0';

	nvs_get_blob(handle, "RSA_PRV", NULL, &size);
	nvs_get_blob(handle, "RSA_PRV", m_rsa_key, &size);
	m_rsa_key[size] = '\0';
	m_rsa_key_size = size + 1;

	nvs_close(handle);
}

static void ota_update_task(void *pvParameter)
{
	esp_err_t err;

	/* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
	esp_ota_handle_t update_handle = 0;
	const esp_partition_t *update_partition = NULL;

	ESP_LOGI(TAG, "Starting OTA example...");

	const esp_partition_t *configured = esp_ota_get_boot_partition();
	const esp_partition_t *running = esp_ota_get_running_partition();

	if (configured != running) {
		ESP_LOGW(TAG,
			 "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
			 configured->address, running->address);
		ESP_LOGW(TAG,
			 "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
	}
	ESP_LOGI(TAG,
		 "Running partition type %d subtype %d (offset 0x%08x)",
		 running->type, running->subtype, running->address);

	/* Wait for the callback to set the CONNECTED_BIT in the
	   event group.
	 */
	xEventGroupWaitBits(g_wifi_event_group, CONNECTED_BIT,
			    false, true, portMAX_DELAY);

      WAIT_FOR_BUTTON_PRESS:
	xEventGroupWaitBits(g_button_event_group, OTA_BIT,
			    false, true, portMAX_DELAY);

	ESP_LOGI(TAG, "Connect to Wifi ! Start to Connect to Server....");

	/* download, decrypt, and, parse the meta file */

	int meta_file_length = 0;
	meta_download(ota_download_data, &meta_file_length);
	meta_decrypt(ota_download_data, &meta_file_length, ota_write_data);

	uint8_t aes_key[32] = { 0 };
	uint8_t aes_iv[16] = { 0 };

	uint64_t firmware_size = 0;
	meta_parse(ota_write_data, aes_key, aes_iv, &firmware_size);

	update_partition = esp_ota_get_next_update_partition(NULL);

	/* download firmware */
	char *url = NULL;
	int url_size = snprintf(NULL, 0, "%s%s", EXAMPLE_SERVER_URL,
				"firmware.bin");
	url = malloc(url_size + 1);
	sprintf(url, "%s%s", EXAMPLE_SERVER_URL, "firmware.bin");

	esp_http_client_config_t config = { 0 };
	config.cert_pem = (char *) m_ca_cert;
	config.client_cert_pem = (char *) m_https_crt;
	config.client_key_pem = (char *) m_https_key;
	config.url = url;
	ESP_LOGI(TAG, "downloading file @ %s", config.url);
	esp_http_client_handle_t client = esp_http_client_init(&config);

	if (client == NULL) {
		ESP_LOGE(TAG, "Failed to initialise HTTP connection");
		goto FAIL_CLEANUP_URL;
	}

	err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s",
			 esp_err_to_name(err));
		goto FAIL_CLEANUP_CLIENT;
	}
	esp_http_client_fetch_headers(client);
	ESP_LOGI(TAG,
		 "Writing to partition subtype %d at offset 0x%x",
		 update_partition->subtype, update_partition->address);
	assert(update_partition != NULL);

	int binary_file_length = 0;
	bool first_chunk = true;
	uint64_t data_remaining = firmware_size;

	/* AES setup */
	esp_aes_context ctx;
	esp_aes_init(&ctx);
	esp_aes_setkey(&ctx, aes_key, 256);

	while (0 < data_remaining) {
		/* download a chunk of BUFFSIZE size */
		int data_read =
		    esp_http_client_read(client, ota_download_data,
					 BUFFSIZE);

		/* something went wrong */
		if (data_read < 0) {
			ESP_LOGE(TAG, "Error: SSL data read error");
			goto FAIL_CLEANUP_AES;
		}

		/* decrypt chunk */
		esp_aes_crypt_cbc(&ctx, ESP_AES_DECRYPT,
				  data_read, aes_iv, (uint8_t *)
				  ota_download_data, (uint8_t *)
				  ota_write_data);

		/* is the chunk the first chunk? */
		if (first_chunk) {
			if (data_read <=
			    sizeof(esp_image_header_t) +
			    sizeof(esp_image_segment_header_t) +
			    sizeof(esp_app_desc_t)) {
				ESP_LOGE(TAG,
					 "received package is not fit len");
				goto FAIL_CLEANUP_AES;
			}
			/* check the version */

			/* if the version isn't newer, reset the update process */
			if (!is_firmware_version_newer
			    (ota_write_data, running)) {
				esp_aes_free(&ctx);
				http_cleanup(client);
				goto WAIT_FOR_BUTTON_PRESS;
			}
			first_chunk = false;
			/* get ready to flash update */
			err =
			    esp_ota_begin(update_partition,
					  OTA_SIZE_UNKNOWN,
					  &update_handle);
			if (err != ESP_OK) {
				ESP_LOGE(TAG,
					 "esp_ota_begin failed (%s)",
					 esp_err_to_name(err));
				goto FAIL_CLEANUP_AES;
			}
			ESP_LOGI(TAG, "esp_ota_begin succeeded");
		}
		/* write chunk into flash */
		err = esp_ota_write(update_handle, (const void *)
				    ota_write_data,
				    (data_remaining <
				     data_read ? data_remaining :
				     data_read));

		if (err != ESP_OK) {
			goto FAIL_CLEANUP_OTA;
		}

		binary_file_length += data_read;
		data_remaining -=
		    (data_remaining <
		     data_read ? data_remaining : data_read);
		ESP_LOGI(TAG, "(%9d / %9llu) flashed", binary_file_length,
			 firmware_size);
	}

	/* clean up */
	esp_aes_free(&ctx);
	free(url);
	url = NULL;
	http_cleanup(client);

	ESP_LOGI(TAG, "Connection closed,all data received");

	ESP_LOGI(TAG, "Total Write binary data length : %d",
		 binary_file_length);

	if (esp_ota_end(update_handle) != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed!");
		goto FAIL_FATAL_ERROR;
	}

	if (esp_partition_check_identity
	    (esp_ota_get_running_partition(), update_partition) == true) {
		ESP_LOGI(TAG,
			 "The current running firmware is same as the firmware just downloaded");
		ESP_LOGI(TAG, "Resetting update process");
		goto WAIT_FOR_BUTTON_PRESS;
	}

	err = esp_ota_set_boot_partition(update_partition);
	if (err != ESP_OK) {
		ESP_LOGE(TAG,
			 "esp_ota_set_boot_partition failed (%s)!",
			 esp_err_to_name(err));
		goto FAIL_FATAL_ERROR;
	}


	led_stop();
	ESP_LOGI(TAG, "Prepare to restart system!");
	esp_restart();
	return;


	/* intended fall throughs */
      FAIL_CLEANUP_OTA:
	esp_ota_end(update_handle);

      FAIL_CLEANUP_AES:
	esp_aes_free(&ctx);
	esp_http_client_close(client);

      FAIL_CLEANUP_CLIENT:
	esp_http_client_cleanup(client);

      FAIL_CLEANUP_URL:
	free(url);
	url = NULL;

      FAIL_FATAL_ERROR:
	task_fatal_error();

}

void ota_init()
{
	load_certificates();

	xTaskCreate(&ota_update_task, "ota_update_task", 8192,
		    NULL, 5, NULL);
}
