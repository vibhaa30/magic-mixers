
import cv2
import numpy as np
import mediapipe as mp
import math
from picamera2 import Picamera2

mpHands = mp.solutions.hands
hands = mpHands.Hands(max_num_hands=2, min_detection_confidence=0.7)
mpDraw = mp.solutions.drawing_utils

picam2 = Picamera2()
picam2.configure(picam2.create_preview_configuration(main={"format": "RGB888", "size": (640, 480)}))
picam2.start()


#definitions
thumb_num = 4
index_num = 8
middle_num = 12
ring_num = 16
pinky_num = 20

angle_max = 45 #based on experimenting. Note my right hand sucks ass and might lack the full range
	#also note that CV is worse at measuring angles than you might except 
num_regions = 4 #kinda arbitrary. change in integration according to what the audio mix team has going on
amp_array = [0,0]
current_feature = 0

def is_extended(markers, tip_id):
	tip = markers.landmark[tip_id]
	mid = markers.landmark[tip_id - 2]
	return (tip.y < mid.y)
	
def is_extended_thumb (markers, tip_id):
	tip = markers.landmark[tip_id]
	mid = markers.landmark[tip_id - 2]
	return (tip.x > mid.x)

def left_hand_feature(markers):
	index = is_extended(markers, index_num)
	middle = is_extended(markers, middle_num)
	ring = is_extended(markers, ring_num)
	pinky = is_extended(markers, pinky_num)
	thumb = is_extended_thumb (markers, thumb_num)
	
	if(ring or pinky or thumb):
		return 0	
	if(index and not middle):
		return 1
	elif (index and middle):
		return 2
	else:
		return 0

def right_hand_amplitude(markers):
	index_point = markers.landmark[8]
	thumb_point = markers.landmark[4]
	thumb_base = markers.landmark[1]
	#find len "opposite"
	dist_o = math.dist(
		(index_point.x, index_point.y),
		(thumb_point.x, thumb_point.y)
	)
	
	#find len "hypotenuse"
	dist_h = math.dist(
		(index_point.x, index_point.y, index_point.z),
		(thumb_base.x, thumb_base.y, thumb_base.z)
	)
	
	amp_angle = math.degrees(math.asin(min(dist_o / dist_h, 1.0)))
	amplitude = math.ceil(amp_angle/(angle_max/num_regions)) - 1
	amplitude = max(0, min(num_regions - 1, amplitude))
	print (amp_angle)
	return (amplitude)

while True:
	frame = picam2.capture_array()
	x, y, c = frame.shape
	frame = cv2.flip(frame, 1)
	result = hands.process(frame)
	
	#grab hand values
	feature = 0
	amplitude = 0
	
	if result.multi_hand_landmarks and result.multi_handedness: #if hand on screen
		for handslms, handedness in zip(result.multi_hand_landmarks, result.multi_handedness):
			label = handedness.classification[0].label
			mpDraw.draw_landmarks(frame, handslms, mpHands.HAND_CONNECTIONS)
			if label == "Left":
				feature = left_hand_feature(handslms)
				current_feature = feature
			elif label == "Right":
				amplitude = right_hand_amplitude(handslms)

	#update store array with
	if(feature): #aka if feature != 0
		amp_array[feature - 1] = amplitude
	
	#print values from array
	print(f"feature 1 amplitude {amp_array[0]}")
	print(f"feature 2 amplitude {amp_array[1]}")
	 
	
	#closing stuff
	cv2.putText(
		frame,                           # image to draw on
		f"feat:{current_feature} amp:{amplitude}",  # the text string
		(10, 50),                        # x,y position in pixels from top-left
		cv2.FONT_HERSHEY_SIMPLEX,        # font style
		1,                               # font size
		(0, 0, 255),                     # colour in BGR — this is red
		2,                               # line thickness
		cv2.LINE_AA                      # antialiasing, makes text smoother
	)
	cv2.imshow("Output", frame)
	if cv2.waitKey(1) == ord('q'):
		break

picam2.stop()
cv2.destroyAllWindows()	
	
