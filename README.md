# dashify

A small application intended to split up an mp4 with h264/aac into chunks suitable to serve to an MPEG-DASH client.

The file must have a gop size shorter than the duration of each segment and each gop has to be closed.
