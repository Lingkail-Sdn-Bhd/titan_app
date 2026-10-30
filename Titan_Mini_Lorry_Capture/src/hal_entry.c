#include <rtthread.h>
#include "hal_data.h"
#include <rtdevice.h>
#include <board.h>
#include "camera_layer.h"
#include "camera_layer_config.h"
#include <lcd_port.h>
#include "yolo_rtthread.h"   /* provides D2_FIX used by lcd_draw_frame */

#if defined(RT_USING_LWIP) && defined(BSP_USING_ETH)
#include <sys/socket.h>
#include <netdb.h>
#include <netdev.h>
#endif

#define DBG_TAG     "main"
#define DBG_LVL     DBG_INFO
#include "rtdbg.h"

#define LED_PIN     BSP_IO_PORT_01_PIN_09
#define KEY_PIN     BSP_IO_PORT_02_PIN_01

#define CAM_WIDTH               640
#define CAM_HEIGHT              480
#define CAM_BYTES_PER_PIXEL     2
#define KEY_DEBOUNCE_TIME       20
#define KEY_COOLDOWN_TIME       500

/* ===== Upload settings: change UPLOAD_HOST to the receiving PC's IP ===== */
#define UPLOAD_HOST             "192.168.0.59"
#define UPLOAD_PORT             5000
#define UPLOAD_INTERVAL_S       10
#define HTTP_UPLOAD_CHUNK_BYTES 1460

static struct rt_completion display_vsync_completion;
static bool key_last_state = true;
static rt_tick_t key_debounce_tick = 0;
static bool key_processed = false;

/* Draw one camera frame on the LCD (no boxes) */
static void lcd_draw_frame(const void *p, int32_t xSize, int32_t ySize)
{
    d2_device *hdl = d2_handle_obj_get();

    d2_framebuffer(hdl, (uint16_t *)&fb_background[0],
                   LCD_WIDTH, LCD_WIDTH, LCD_HEIGHT, d2_mode_rgb565);
    d2_selectrenderbuffer(hdl, d2_renderbuffer_get());
    d2_cliprect(hdl, 0, 0, LCD_WIDTH, LCD_HEIGHT);
    d2_setblitsrc(hdl, (void *)p, xSize, xSize, ySize, d2_mode_rgb565);
    d2_blitcopy(hdl, xSize, ySize, 0, 0,
                D2_FIX(LCD_WIDTH), D2_FIX(LCD_HEIGHT), D2_FIX(0), D2_FIX(0), 0);
    d2_executerenderbuffer(hdl, d2_renderbuffer_get(), 0);
    d2_flushframe(hdl);
}

/* Required by the display driver (FSP callback) */
void DisplayVsyncCallback(display_callback_args_t * p_args)
{
    if ((NULL != p_args) && (DISPLAY_EVENT_LINE_DETECTION == p_args->event))
    {
        rt_completion_done(&display_vsync_completion);
    }
}

/* True once Ethernet link is up and DHCP gave us an IP */
static rt_bool_t network_ready(void)
{
#if defined(RT_USING_LWIP) && defined(BSP_USING_ETH)
    return (netdev_default && netdev_is_link_up(netdev_default) &&
            !ip_addr_isany(&(netdev_default->ip_addr)));
#else
    return RT_FALSE;
#endif
}

#if defined(RT_USING_LWIP) && defined(BSP_USING_ETH)
static int socket_send_all(int sock, const uint8_t *data, int len)
{
    int sent_total = 0;
    while (sent_total < len)
    {
        int sent = send(sock, data + sent_total, len - sent_total, 0);
        if (sent <= 0)
        {
            return -1;
        }
        sent_total += sent;
    }
    return 0;
}

/* Send one RGB565 frame to receive_face_http.py */
static int upload_frame(const uint8_t *p_img)
{
    int image_size = CAM_WIDTH * CAM_HEIGHT * CAM_BYTES_PER_PIXEL;
    struct hostent *host;
    struct sockaddr_in server_addr;
    char header[384];
    int header_len;
    int sent_total = 0;
    int timeout = 5000;
    int sock;

    host = gethostbyname(UPLOAD_HOST);
    if (host == RT_NULL)
    {
        rt_kprintf("upload failed: resolve %s failed\n", UPLOAD_HOST);
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        rt_kprintf("upload failed: socket error\n");
        return -1;
    }

    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t) UPLOAD_PORT);
    server_addr.sin_addr = *((struct in_addr *) host->h_addr);
    rt_memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

    if (connect(sock, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) < 0)
    {
        rt_kprintf("upload failed: connect %s:%d failed\n", UPLOAD_HOST, UPLOAD_PORT);
        closesocket(sock);
        return -1;
    }

    header_len = rt_snprintf(header, sizeof(header),
                             "POST /upload HTTP/1.1\r\n"
                             "Host: %s:%d\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Length: %d\r\n"
                             "X-Width: %d\r\n"
                             "X-Height: %d\r\n"
                             "X-Format: RGB565\r\n"
                             "X-Faces: 0\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             UPLOAD_HOST, UPLOAD_PORT, image_size, CAM_WIDTH, CAM_HEIGHT);

    if ((header_len <= 0) || (header_len >= (int) sizeof(header)) ||
        (socket_send_all(sock, (const uint8_t *) header, header_len) != 0))
    {
        rt_kprintf("upload failed: send header failed\n");
        closesocket(sock);
        return -1;
    }

    while (sent_total < image_size)
    {
        int chunk = image_size - sent_total;
        if (chunk > HTTP_UPLOAD_CHUNK_BYTES)
        {
            chunk = HTTP_UPLOAD_CHUNK_BYTES;
        }
        if (socket_send_all(sock, p_img + sent_total, chunk) != 0)
        {
            rt_kprintf("upload failed: sent=%d/%d\n", sent_total, image_size);
            closesocket(sock);
            return -1;
        }
        sent_total += chunk;
    }

    closesocket(sock);
    return 0;
}
#else
static int upload_frame(const uint8_t *p_img)
{
    RT_UNUSED(p_img);
    rt_kprintf("upload disabled: enable Ethernet in RT-Thread Settings\n");
    return -1;
}
#endif

void hal_entry(void)
{
    LOG_I("================================================");
    LOG_I("Titan Mini lorry capture: upload every %d s", UPLOAD_INTERVAL_S);
    LOG_I("Target: http://%s:%d/upload", UPLOAD_HOST, UPLOAD_PORT);
    LOG_I("================================================");

    fsp_err_t fsp_status = camera_init(false);
    if (FSP_SUCCESS != fsp_status)
    {
        LOG_E("camera_init fail! err=%d", fsp_status);
        return;
    }

    camera_image_buffer_initialize();

    fsp_status = R_VIN_CaptureStart(&g_cam_vin_ctrl, NULL);
    if (FSP_SUCCESS != fsp_status)
    {
        LOG_E("R_VIN_CaptureStart fail! err=%d", fsp_status);
        return;
    }

    rt_thread_mdelay(100);
    rt_completion_init(&display_vsync_completion);
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    key_last_state = rt_pin_read(KEY_PIN);

    static rt_tick_t focus_done_tick = 0;
    static rt_tick_t last_trigger_tick = 0;
    static bool focus_locked = false;
    const rt_tick_t release_delay = rt_tick_from_millisecond(600);
    const rt_tick_t upload_interval = rt_tick_from_millisecond(UPLOAD_INTERVAL_S * 1000);
    rt_tick_t last_upload_tick = rt_tick_get() - upload_interval;  /* upload ASAP once network is up */
    uint32_t uploaded = 0;
    bool led_status = false;

    rt_kprintf("[AF] Press key to trigger auto focus!\n");

    while (1)
    {
        rt_tick_t current_tick = rt_tick_get();

        /* ---- autofocus button (unchanged from example) ---- */
        bool key_current = rt_pin_read(KEY_PIN);
        if (key_current != key_last_state)
        {
            key_debounce_tick = current_tick;
            key_processed = false;
        }
        key_last_state = key_current;

        if (!key_current && !key_processed &&
            (current_tick - key_debounce_tick) >= rt_tick_from_millisecond(KEY_DEBOUNCE_TIME))
        {
            key_processed = true;
            if ((current_tick - last_trigger_tick) >= rt_tick_from_millisecond(KEY_COOLDOWN_TIME))
            {
                rt_kprintf("[AF] Triggering auto focus...\n");
                if (OV5640_auto_focus() == RT_EOK)
                {
                    focus_locked = true;
                    focus_done_tick = current_tick;
                    last_trigger_tick = current_tick;
                    rt_kprintf("[AF] Focus locked!\n");
                }
            }
        }

        if (focus_locked && (current_tick - focus_done_tick) >= release_delay)
        {
            uint8_t val;
            rdSensorReg16_8(0x3029, &val);
            if (val == 0x10)
            {
                wrSensorReg16_8(0x3022, 0x06);
            }
            focus_locked = false;
            rt_kprintf("[I/AF] Release Focus\n");
        }

        /* ---- get latest camera frame ---- */
        if (!camera_capture_post_process())
        {
            rt_thread_mdelay(1);
            continue;
        }

        uint16_t *frame = (uint16_t *)camera_data_ready_buffer_pointer_get();
        if (!frame)
        {
            rt_thread_mdelay(1);
            continue;
        }

        /* ---- every UPLOAD_INTERVAL_S: upload this frame ---- */
        if (network_ready() && ((current_tick - last_upload_tick) >= upload_interval))
        {
            last_upload_tick = current_tick;
            if (upload_frame((const uint8_t *) frame) == 0)
            {
                uploaded++;
                rt_kprintf("capture #%d uploaded\n", uploaded);
                led_status = !led_status;               /* LED toggles = upload OK */
                rt_pin_write(LED_PIN, led_status ? PIN_HIGH : PIN_LOW);
            }
        }

        /* ---- show on LCD ---- */
        lcd_draw_frame(frame, CAM_WIDTH, CAM_HEIGHT);

        rt_thread_mdelay(5);
    }
}
