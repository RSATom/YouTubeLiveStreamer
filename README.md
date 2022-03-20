[![youtube-live-streamer](https://snapcraft.io/youtube-live-streamer/badge.svg)](https://snapcraft.io/youtube-live-streamer)

# YouTube Live Streamer
Streams IP Cam to YouTube Live.

## Getting started
1. Install snap: `sudo snap install youtube-live-streamer --edge`
2. Get `stream key` in YouTube Studio (https://studio.youtube.com/channel/UC/livestreaming)
3. Open config file for edit: `sudoedit /var/snap/youtube-live-streamer/common/live-streamer.conf` 
4. Replace `source` value with desired RTSP URL
5. Uncomment `youtube-stream-key: "xxxx-xxxx-xxxx-xxxx-xxxx"` line by removing `#` at the beginning and replace `xxxx-xxxx-xxxx-xxxx-xxxx` with your `stream key`
6. Restart snap: `sudo snap restart youtube-live-streamer`

## Troubleshooting
* Look to the logs with `sudo snap logs youtube-live-streamer` or `sudo snap logs youtube-live-streamer -f`
