#Additional Information

This folder contains the files that were used for testing the code, as well as the plots that I used to
convince myself that my RDT sender and receiver were both working correctly. A large video file (size 330 MB)
was also used for testing, and was always transmitted correctly (checked with md5sum), but was not included.
The throughput plot shows good utilization of the network, while the CWND plot shows slow start followed by
congestion avoidance, and the ssthreshold being set to half the window size upon packet loss.
