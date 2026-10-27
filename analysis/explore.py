import pandas as pd, numpy as np

df = pd.read_csv("smh.csv")

# regular trading hours only (9:30–16:00), ns since midnight
OPEN, CLOSE = 34_200_000_000_000, 57_600_000_000_000
df = df[(df.ts >= OPEN) & (df.ts < CLOSE)].reset_index(drop=True)

# sanity: no crossed book
assert (df.bid_px < df.ask_px).all(), "crossed book!"

# derived quantities (prices are in 1/10000 dollar; 100 units = 1 cent)
df["spread"]     = df.ask_px - df.bid_px          # in price units
df["dt_us"]      = df.ts.diff() / 1_000.0          # inter-event gap, microseconds

pcts = [50, 90, 99, 99.99]
print("spread (cents):")
print(np.percentile(df.spread.dropna(), pcts) / 100)
print("\ninter-event time (µs):")
print(np.percentile(df.dt_us.dropna(), pcts))

bid, ask = df.bid_px, df.ask_px
bsz, asz = df.bid_sz, df.ask_sz

df["mid"]        = (bid + ask) / 2.0
df["microprice"] = (bid*asz + ask*bsz) / (bsz + asz)   # note the cross-weight
df["imb"]        = (bsz - asz) / (bsz + asz)            # order-book imbalance, [-1,1]
df["micro_tilt"] = df["microprice"] - df["mid"]         # microprice signal

for k in [1, 5, 10, 50, 100]:
    fwd = df["mid"].shift(-k) - df["mid"]               # STRICTLY future mid move
    m = fwd.notna()
    ic_imb   = df["imb"][m].corr(fwd[m], method="spearman")
    ic_micro = df["micro_tilt"][m].corr(fwd[m], method="spearman")
    print(f"k={k:>3}  IC_imb={ic_imb:+.4f}  IC_micro={ic_micro:+.4f}  n={m.sum()}")

mid_t  = df["mid"]
sig    = df["imb"]
split  = len(df)//2
for name, s in [("AM", slice(0, split)), ("PM", slice(split, None))]:
    seg = df.iloc[s]
    fwd = seg["mid"].shift(-10) - seg["mid"]
    m = fwd.notna()
    print(name, "IC@10 =", round(seg["imb"][m].corr(fwd[m], method="spearman"), 4))


fwd_cents = (df["mid"].shift(-10) - df["mid"]) / 100
m = fwd_cents.notna()
b = np.polyfit(df["imb"][m], fwd_cents[m], 1)
print("slope (cents per unit imbalance):", round(b[0], 3))

import matplotlib.pyplot as plt
ks  = [1,5,10,50,100]
ics = [0.2539, 0.2554, 0.2182, 0.1489, 0.1145]
plt.figure(figsize=(6,4))
plt.plot(ks, ics, "o-")
plt.xscale("log")
plt.xlabel("horizon k (events)"); plt.ylabel("information coefficient")
plt.title("SMH/BX — order-book imbalance alpha decay")
plt.grid(True, alpha=0.3); plt.tight_layout()
plt.savefig("analysis/decay.png", dpi=130)