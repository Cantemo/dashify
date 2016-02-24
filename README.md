# dashify

A small application intended to split up an mp4 with h264/aac into chunks suitable to serve to an MPEG-DASH client.

The file must have a gop size shorter than the duration of each segment and each gop has to be closed.

## usage

```
usage: ./dashify [--stream streamid] [--duration duration] [--segment number] [--init] [--codec] input output

This application extracts a single segment from a single channel suitable for playing in a DASH stream

  --stream streamid	    The stream identifier to extract on the form [av]:[n]. Defaults to v:0
  --duration		    The requested duration of the segment, in milliseconds
  --segment number	    The segment number. Will seek to the requested segment
  --init    		    Extract the init segment with the moov header
  --codec		    Print the codec string for the requested stream instead of the data
```