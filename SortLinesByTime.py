with open("shared_log.txt") as f:
    lines = f.readlines()
lines.sort(key=lambda l: l[1:24])  # Adjust slice to match your timestamp format
with open("shared_log_sorted.txt", "w") as f:
    f.writelines(lines)
