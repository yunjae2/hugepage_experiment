#!/usr/bin/env python
import matplotlib.pyplot as plt

def main():
    page_type = ["base", "huge"]
    access_type = ["seq", "rand"]
    sizes = []
    for i in range(2, 20):
        sizes.append(2**i)

    data = {}
    for page in page_type:
        data[page] = {}
        for access in access_type:
            data[page][access] = {}
            for size in sizes:
                temp = {}
                temp["dtlb_walk_cycles"] = float(0)
                temp["cpu_cycles"] = float(0)
                for i in range(0, 5):
                    filename = "result/%s-%s-%d-%d.out" % (page, access, size, i)
                    with open(filename, "r") as f:
                        for line in f:
                            line = line.strip()
                            if not line:
                                continue
                            fields = line.split()
                            if fields[0] == "dtlb_miss_walk_cycles:":
                                dtlb_walk_cycles = int(fields[1])
                                temp["dtlb_walk_cycles"] += float(fields[1])
                            elif fields[0] == "cpu_cycles:":
                                cpu_cycles = float(fields[1])
                                temp["cpu_cycles"] += float(fields[1])
                temp["dtlb_walk_cycles"] /= 5
                temp["cpu_cycles"] /= 5
                temp["dtlb_walk_ratio"] = 100 * temp["dtlb_walk_cycles"] / temp["cpu_cycles"]
                data[page][access][size] = temp

    for page in page_type:
        for access in access_type:
            filename_dtlb="%s-%s-dtlb.csv" % (page, access)
            f_dtlb = open(filename_dtlb, "w")
            for size in sizes:
                nsize = str(size) + "KB"
                if size >= 1024:
                    nsize = str(size / 1024) + "MB"
                f_dtlb.write("%s, %s\n" % (nsize, data[page][access][size]["dtlb_walk_ratio"]))
            f_dtlb.close()

    for page in page_type:
        for access in access_type:
            for measure in ["dtlb"]:
                with open("%s-%s-%s.csv" % (page, access, measure), "r") as f:
                    sizes = []
                    values = []
                    for line in f:
                        line = line.strip()
                        if not line:
                            continue
                        fields = line.split()
                        size = fields[0][0:-1]
                        value = float(fields[1])
                        sizes.append(size)
                        values.append(value)

                    x_pos = [i for i, _ in enumerate(sizes)]
                    plt.bar(x_pos, values, align = 'center')
                    plt.title("%s page, %s access" % (page, access))
                    plt.ylabel("Cycle % used for page walk")
                    plt.xlabel("Object size")
                    plt.xticks(x_pos, sizes, rotation = 45)
                    plt.tight_layout()
                    plt.savefig("%s-%s-%s.png" % (page, access, measure))
                    plt.clf()


if __name__ == "__main__":
    main()
