#!/usr/bin/env python

import numpy as np
from scipy import stats

class InterQuartileRange:
    def __init__(self, low, high, extend = False):
        self.low = low
        self.high = high
        # extend is 1.5 extension of IQR
        self.extend = extend

    def remOutliers(self, vector):
        svect = np.sort(vector)
        q1 = stats.scoreatpercentile(svect, self.low)
        q3 = stats.scoreatpercentile(svect, self.high)

        # match the values \in svect which are closer to q[1|3]
        # (q1, q3)
        q1_pos = -1
        q3_pos = -1
        cur_pos = 0
        for i in svect:
            if q1_pos != -1 and q3_pos != -1:
                break
            if q1_pos == -1 and i > q1:
                q1_pos = cur_pos
            if q3_pos == -1 and q3 < i:
                q3_pos = cur_pos

            cur_pos += 1

        if self.extend == True:
            # 1.5 IQR outliers elimination
            eiqr = (svect[q3_pos] - svect[q1_pos]) * 1.5
            eq1 = svect[q1_pos] - eiqr
            if eq1 < svect[0]:
                eq1 = svect[0]
            eq3 = svect[q3_pos] + eiqr
            if eq3 > svect[len(svect) - 1]:
                eq3 = svect[len(svect) - 1]
            # match the values \in svect which are closer to eq[1|3]
            q1_pos = -1
            q3_pos = -1
            cur_pos = 0
            for i in svect:
                if q1_pos != -1 and q3_pos != -1:
                    break
                if q1_pos == -1 and i > eq1:
                    q1_pos = cur_pos
                if q3_pos == -1 and eq3 < i:
                    q3_pos = cur_pos

                cur_pos += 1

        return svect[q1_pos : q3_pos]
