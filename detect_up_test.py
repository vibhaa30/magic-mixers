# Import the necessary Packages and scritps for this software to run (Added speak in
# there too as an easer egg)
import cv2
from collections import Counter
from module import findnameoflandmark, findpostion, speak
import math

# Use CV2 Functionality to create a Video stream and add some values + variables
cap = cv2.VideoCapture(0)
tip = [8, 12, 16, 20]
tipname = [8, 12, 16, 20]
fingers = []
finger = []
code = "base"


#def shape()
# Create an infinite loop which will produce the live feed to our desktop and that will search for hands
def shape(code_str: str) -> int:
    if code_str == "invalid":
        return 0
    elif code_str == "pointer":
        return 1
    elif code_str == "duces":
        return 2
    else:
        return 0


while True:
    ret, frame = cap.read()
    # Unedit the below line if your live feed is produced upsidedown
    # flipped = cv2.flip(frame, flipCode = -1)

    # Determines the frame size, 640 x 480 offers a nice balance between speed and accurate identification
    frame1 = cv2.resize(frame, (640, 480))

    # Below is used to determine location of the joints of the fingers
    a = findpostion(frame1)
    b = findnameoflandmark(frame1)

    #code = "base"

    if len(b and a) != 0:

        error = 0

        if a[16][2:] < a[14][2:] or a[20][2:] < a[18][2:]: #or a[4][2:] < a[2][2:]:
            #print("wrong fingers up")
            error = 1
            code = "invalid"

        if  error == 0 and a[tip[0]][2:] < a[tip[0] - 2][2:] and a[tip[1]][2:] > a[tip[1] - 2][2:]:
            #print(" only first finger is up")
            code = "pointer"
        if error == 0 and a[tip[0]][2:] < a[tip[0] - 2][2:] and a[tip[1]][2:] < a[tip[1] - 2][2:]:
            #print("first two fingers up")
            code = "duces"
        #print("cycle end")
    else:
        code = "invalid"

    print(code)
    print(" out out value: ", shape(code))

    cv2.imshow("Frame", frame1);
    key = cv2.waitKey(1) & 0xFF