import numpy as np
import csv
import os
OUT_FILE = "events.dat"

#with open(os.path.join(os.path.dirname(__file__),OUT_FILE),"r") as csv_file:
#    data =  np.genfromtxt(csv_file, delimiter='\t')

with open(os.path.join(os.path.dirname(__file__),"events","0.pgm"),"r") as csv_file:
    lines = csv_file.readlines()

meta_data = np.fromstring(lines[4][1:], dtype=float, sep='\t')
matrix =  np.genfromtxt(lines[5:], delimiter='\t').astype(int)

print(meta_data)
print(matrix)
#print(data[:,1].astype(int))
#print(np.bincount((data[:,1].astype(int))))
#print(len(data[:,1].astype(int)))