import numpy as np

def run_tpt_allpass_simulation():
    print("=== TPT ALL-PASS FILTER STABILITY TEST ===")
    num_samples = 256
    # Impulse input
    x = np.zeros(num_samples)
    x[0] = 1.0

    test_G = [0.1, 0.5, 0.9, 0.99, 1.0, 1.001, 1.01, 1.1]

    for G in test_G:
        s1 = 0.0
        s2 = 0.0
        y = np.zeros(num_samples)
        exploded = False
        max_val = 0.0

        for n in range(num_samples):
            # Stage 1
            v0 = G * (x[n] - s1)
            y_lp0 = v0 + s1
            s1 = y_lp0 + v0
            stage1_out = 2.0 * y_lp0 - x[n]

            # Stage 2
            v1 = G * (stage1_out - s2)
            y_lp1 = v1 + s2
            s2 = y_lp1 + v1
            stage2_out = 2.0 * y_lp1 - stage1_out

            y[n] = stage2_out
            val = abs(stage2_out)
            if val > max_val:
                max_val = val
            if np.isnan(val) or np.isinf(val) or val > 1e10:
                exploded = True
                print(f"G = {G:7.4f}: EXPLODED at sample {n}! max_val = {max_val}")
                break

        if not exploded:
            energy = np.sum(y**2)
            print(f"G = {G:7.4f}: Stable. Final s1={s1:10.4e}, s2={s2:10.4e}, Output Energy={energy:10.4f}, Max Abs={max_val:10.4f}")

if __name__ == "__main__":
    run_tpt_allpass_simulation()
