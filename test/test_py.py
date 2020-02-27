a = [0 for i in range(50005)]
a[0] = a[1] = 1
for i in range(2, 50005):
    a[i] = a[i - 1] + a[i - 2];
print(a)