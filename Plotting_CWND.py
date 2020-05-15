import numpy as np
import matplotlib.pyplot as plt
import csv
import os

path = os.getcwd()

time = []
cwnd = []
ssthresh = []

with open('CWND.csv', newline='\n') as csvfile:
	reader = csv.reader(csvfile, delimiter=',')
	for row in reader:
		time.append(int(row[0])/1000)
		cwnd.append(int(row[1]))
		ssthresh.append(int(row[2]))

plt.plot(time, cwnd, lw=2, color='r')

plt.ylabel("Current Window (Mbps)")
plt.xlabel("Time (s)")
# plt.xlim([0,300])
plt.grid(True, which="both")
plt.savefig(path+'/current_window_plot.pdf',dpi=1000,bbox_inches='tight')

