#!/usr/bin/python
"""
a + b + c = 1000;
a^2 + b^2 = c^2;
[a, b, c]
"""
#!/usr/bin/python
import time

def main():
    r_s = [];

    s_t = int(round(time.time() * 1000))
    for a in range(0, 1001):
        for b in range(0, 1001):
            r = {}
            c = 1000 - a - b
            if (a**2 + b**2) == c**2:
                r["a"] = a
                r["b"] = b
                r["c"] = c

                r_s.append(r)

    e_t = int(round(time.time() * 1000))
    print("time : %dms" %(e_t - s_t))
    print("r_s : " + str(len(r_s)))
    for i  in r_s:
        print("a = %d, b = %d, c = %d;" %(i.get("a"),
               i.get("b"), i.get("c")))

if __name__ == "__main__":
    main()
