const [id, csrf, codec, done] = arguments;
(async () => {
    const peer = new RTCPeerConnection({ iceServers: [] });
    const video = document.createElement("video");
    video.muted = true;
    document.body.append(video);
    const start = performance.now();
    try {
        const channel = peer.addTransceiver("video", { direction: "recvonly" });
        channel.setCodecPreferences(
            RTCRtpReceiver.getCapabilities("video").codecs.filter(
                (c) =>
                    c.mimeType.toLowerCase() === "video/" + codec &&
                    (codec === "h265" || /packetization-mode=1/.test(c.sdpFmtpLine ?? "")),
            ),
        );
        peer.ontrack = ({ track }) => {
            video.srcObject = new MediaStream([track]);
            void video.play();
        };
        const frame = new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(Error("No compatible video frame")), 12000);
            video.requestVideoFrameCallback((_, f) => {
                clearTimeout(timer);
                const canvas = document.createElement("canvas");
                canvas.width = f.width;
                canvas.height = f.height;
                const ctx = canvas.getContext("2d");
                ctx.drawImage(video, 0, 0);
                const pixels = ctx.getImageData(0, 0, f.width, f.height).data;
                let maximum = 0;
                for (let i = 0; i < pixels.length; i += 4) {
                    maximum = Math.max(maximum, pixels[i], pixels[i + 1], pixels[i + 2]);
                }
                resolve({
                    width: f.width,
                    height: f.height,
                    maximum,
                    firstFrameMs: Math.round(performance.now() - start),
                });
            });
        });
        await peer.setLocalDescription(await peer.createOffer());
        while (peer.iceGatheringState !== "complete")
            await new Promise((r) => peer.addEventListener("icegatheringstatechange", r, { once: true }));
        const result = await fetch("/api/v1/media-sessions/" + id + "/offer", {
            method: "POST",
            headers: { "Content-Type": "application/sdp", "X-CSRF-Token": csrf },
            body: peer.localDescription.sdp,
        });
        if (!result.ok) throw Error("SDP answer " + result.status);
        await peer.setRemoteDescription({ type: "answer", sdp: await result.text() });
        return await frame;
    } finally {
        peer.close();
        video.remove();
    }
})().then(done, (e) => done({ error: String(e) }));
