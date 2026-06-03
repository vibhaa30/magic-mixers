
import numpy as np
import pandas as pd
from sklearn.metrics import (
    classification_report, f1_score, precision_score,
    recall_score, confusion_matrix, ConfusionMatrixDisplay,
)
import matplotlib.pyplot as plt


CSV_PATH = "https://raw.githubusercontent.com/sokaryy/hand-gesture-classification-hagrid/main/data/hand_landmarks_data.csv"

HAGRID_MAP = {
    "one":    1,
    "peace":  2,   # index + middle
    "two_up": 2,
    "three":  3,
    "three2": 3,
    "four":   4,
    "fist":   0,
    # stop, palm, call, dislike, like, mute, ok, rock, etc. -> excluded
}

HAGRID_RELEVANT = set(HAGRID_MAP.keys())

def map_hagrid_label(label):
    return HAGRID_MAP.get(str(label).strip().lower(), None)  # None = exclude

def detect_format(csv_path):
    if csv_path.startswith("http"):
        first_line = pd.read_csv(csv_path, nrows=1).columns[0]
        has_header = not str(first_line).lstrip("-").replace(".", "").isdigit()
        df = pd.read_csv(csv_path)
        return ("hagrid" if has_header else "original"), df
    else:
        with open(csv_path) as f:
            first_line = f.readline()
        has_header = not first_line.split(",")[0].strip().lstrip("-").replace(".", "").isdigit()
        if has_header:
            return "hagrid", pd.read_csv(csv_path)
        else:
            return "original", pd.read_csv(csv_path, header=None)

class LM:
    def __init__(self, x, y):
        self.x = x
        self.y = y

class LandmarkRow:
    def __init__(self, row, fmt):
        self._lms = {}
        for i in range(21):
            if fmt == "original":
                x = row[i * 2 + 1]
                y = row[i * 2 + 2]
            else:
                x = row[i * 3]
                y = row[i * 3 + 1]
            self._lms[i] = LM(x, y)

    @property
    def landmark(self):
        return self

    def __getitem__(self, idx):
        return self._lms[idx]

def model_a(lm_row):
    def y(i): return lm_row.landmark[i].y
    if y(16) < y(14) or y(20) < y(18):
        return 0
    if y(8) < y(6) and y(12) > y(10):
        return 1
    if y(8) < y(6) and y(12) < y(10):
        return 2
    return 0

def is_extended(lm_row, tip_id):
    return lm_row.landmark[tip_id].y < lm_row.landmark[tip_id - 2].y

def model_b(lm_row):
    index  = is_extended(lm_row, 8)
    middle = is_extended(lm_row, 12)
    ring   = is_extended(lm_row, 16)
    pinky  = is_extended(lm_row, 20)
    if index and middle and ring and pinky: return 4
    if index and middle and ring:           return 3
    if index and middle:                    return 2
    if index:                               return 1
    return 0

def section(title):
    print("\n" + "=" * 64)
    print(f"  {title}")
    print("=" * 64 + "\n")

def print_metrics(y_true, y_pred, labels, names):
    print(classification_report(y_true, y_pred, labels=labels,
                                target_names=names, zero_division=0))
    p = precision_score(y_true, y_pred, average="macro", labels=labels, zero_division=0)
    r = recall_score   (y_true, y_pred, average="macro", labels=labels, zero_division=0)
    f = f1_score       (y_true, y_pred, average="macro", labels=labels, zero_division=0)
    print(f"Macro averages:  Precision={p:.4f}  Recall={r:.4f}  F1={f:.4f}")
    return p, r, f

print(f"\n{'='*64}")
print("  Hand Gesture CV Model Evaluation — Model A vs Model B")
print(f"{'='*64}")

fmt, df = detect_format(CSV_PATH)
print(f"\nFile    : {CSV_PATH}")
print(f"Format  : {fmt}")
print(f"Samples : {len(df)} (before filtering)\n")

class_names = {0:"fist(0)", 1:"one(1)", 2:"two(2)", 3:"three(3)", 4:"four(4)"}

if fmt == "original":
    y_true_raw = df.iloc[:, 0].values.astype(int)
    feat_rows  = df.values

else:  # hagrid — exclude irrelevant gestures and class 5
    mapped     = np.array([map_hagrid_label(l) for l in df["label"].values])
    keep_mask  = np.array([m is not None for m in mapped])
    dropped    = (~keep_mask).sum()
    y_true_raw = mapped[keep_mask].astype(int)
    feat_rows  = df.drop(columns=["label"]).values.astype(float)[keep_mask]
    print(f"Dropped {dropped} irrelevant samples (stop, palm, call, rock, etc.)")
    print(f"Kept    {keep_mask.sum()} samples\n")
    print("Class distribution after filtering:")
    for cls, cnt in pd.Series(y_true_raw).value_counts().sort_index().items():
        print(f"  class {cls} [{class_names[cls]}]: {cnt} samples")

# Build landmark wrappers
lm_rows = [LandmarkRow(row, fmt) for row in feat_rows]

# Run both models
pred_a = np.array([model_a(lm) for lm in lm_rows])
pred_b = np.array([model_b(lm) for lm in lm_rows])

all_cls   = [0, 1, 2, 3, 4]
all_names = [class_names[c] for c in all_cls]

# ---- Model A: 3-class (collapse 3 and 4 into 0) ----
y_true_3 = np.where(y_true_raw > 2, 0, y_true_raw)
labels_3 = [0, 1, 2]
names_3  = ["other/fist (0)", "pointer (1)", "duces (2)"]

section("MODEL A (detect_up_test.py) — 3-class scope")
pA, rA, fA = print_metrics(y_true_3, pred_a, labels_3, names_3)

# ---- Model B: full 5-class ----
section("MODEL B (hand_command_demo.py) — 5-class scope (0-4)")
pB, rB, fB = print_metrics(y_true_raw, pred_b, all_cls, all_names)

# ---- Summary ----
section("COMPARISON SUMMARY (macro averages)")
print(f"{'Model':<36} {'Precision':>10} {'Recall':>10} {'F1':>10}")
print("-" * 70)
print(f"{'Model A — detect_up_test (3-class)':<36} {pA:>10.4f} {rA:>10.4f} {fA:>10.4f}")
print(f"{'Model B — hand_command_demo (5-class)':<36} {pB:>10.4f} {rB:>10.4f} {fB:>10.4f}")

# ---- Confusion matrices ----
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

cm_a = confusion_matrix(y_true_3, pred_a, labels=labels_3)
ConfusionMatrixDisplay(cm_a, display_labels=["other/fist", "pointer", "duces"]).plot(
    ax=axes[0], colorbar=False)
axes[0].set_title("Model A — detect_up_test.py (3-class)", fontsize=11)

cm_b = confusion_matrix(y_true_raw, pred_b, labels=all_cls)
ConfusionMatrixDisplay(cm_b, display_labels=all_names).plot(
    ax=axes[1], colorbar=False)
axes[1].set_title("Model B — hand_command_demo.py (5-class)", fontsize=11)
axes[1].tick_params(axis="x", labelsize=8)

plt.suptitle(f"{fmt.upper()} dataset  |  n={len(feat_rows)}", fontsize=11)
plt.tight_layout()
plt.show()
