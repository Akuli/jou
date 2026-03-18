# Visualizes the algorithm in "FIGURE 2" of the Myers 1986 paper. The paper is
# named "An O(ND) Difference Algorithm and Its Variations" and can easily be
# found online.

import time
import tkinter

# We want to compute the diff that represents changing a to b.

# This is the example from the Myers paper.
#a = "abcabba"
#b = "cbabac"

# Typical situation in joutest. Expected output is long, actual output is a
# short error message.
a = "javascript"
b = "py"

scale = 50

canvas = tkinter.Canvas(width=(len(a) + 4)*scale, height=(len(b) + 4)*scale, bg="black")
canvas.pack(fill="both", expand=True)

def point_to_canvas(x, y):
    return ((x + 1)*scale, (y + 1)*scale)

# Draw background grid
for x in range(len(a) + 1):
    canvas.create_line(point_to_canvas(x, 0), point_to_canvas(x, len(b)), fill="#444")
for y in range(len(b) + 1):
    canvas.create_line(point_to_canvas(0, y), point_to_canvas(len(a), y), fill="#444")

# Mark rows and columns of grid with letters
for i, letter in enumerate(a):
    canvas.create_text(point_to_canvas(i + 0.5, -0.2), text=letter, fill="#444")
for i, letter in enumerate(b):
    canvas.create_text(point_to_canvas(-0.2, i + 0.5), text=letter, fill="#444")

# Draws diagonal line x-y = k
# The line is tagged with "diagline", so canvas.delete("diagline") removes it.
def draw_diagline(k):
    canvas.delete("diagline")  # Only one can exist at a time
    canvas.create_line(
        # Line equation can be rewritten as y = x-k
        *point_to_canvas(-100, -100 - k), *point_to_canvas(100, 100 - k),
        fill="#ff0", tags="diagline")

# Draws an arrow from (x1,y1) to (x2,y2)
# Returns canvas item ID so arrow color can be changed later.
def draw_arrow(x1, y1, x2, y2) -> None:
    # Don't go all the way to (x2,y2) so there's room for text
    dx = x2 - x1
    dy = y2 - y1
    x1 += 0.2*dx
    y1 += 0.2*dy
    x2 -= 0.2*dx
    y2 -= 0.2*dy

    return canvas.create_line(
        *point_to_canvas(x1, y1), *point_to_canvas(x2, y2),
        width=2, arrow="last", fill="white",
    )

def draw_text(x, y, text) -> None:
    canvas.create_text(point_to_canvas(x, y), text=text, fill="white")

# This is the code from the original Myers paper (FIGURE 2) with the
# following modifications:
#   - Added comments
#   - Less clever and more explicit way to start the algorithm
#   - Added arrow drawing and animating
#   - Backtracking to find the diff by keeping track of arrows drawn
MAX = len(a) + len(b)
V = [None] * (2*MAX + 1)  # Python's negative indexing will be used
arrows = []

def pause() -> None:
    end = time.monotonic() + 0.5
    while time.monotonic() < end:
        canvas.update()

# D is the number of non-diagonal (right or down) arrows at the end of the iteration.
for D in range(MAX + 1):
    for k in range(-D, D + 1, 2):
        draw_diagline(k)
        if D == 0:
            # No arrows yet
            x = y = 0
        elif k == -D or (k != D and V[k-1] < V[k+1]):
            # Arrow down (x doesn't change, y increases, k=x-y decreases)
            # This is an addition because it consumes b without consuming a.
            x = V[k+1]
            y = x-k
            draw_text(x, y, D)
            arrows.append((x, y, '+', draw_arrow(x, y-1, x, y)))
        else:
            # Arrow right (x increases, y doesn't change, k=x-y increases)
            # This is a removal because it consumes a without consuming b.
            x = V[k-1] + 1
            y = x-k
            draw_text(x, y, D)
            arrows.append((x, y, '-', draw_arrow(x-1, y, x, y)))
        pause()

        while x < len(a) and y < len(b) and a[x] == b[y]:
            # Diagonal arrow (x increases, y increases, k=x-y doesn't change)
            # These are unchanged lines because they consume both strings.
            (x, y) = (x+1, y+1)
            draw_text(x, y, D)
            arrows.append((x, y, ' ', draw_arrow(x-1, y-1, x, y)))
            pause()

        V[k] = x
        if x == len(a) and y == len(b):
            print("End found!!!")
            pause()
            canvas.delete("diagline")
            pause()
            pause()

            # Now we need to backtrack through the algorithm to figure out
            # the optimal path from top left corner to bottom right corner.
            #
            # I asked an LLM to prove mathematically that the end point of
            # an arrow uniquely identifies an arrow. It gave a convincing
            # looking proof, so let's assume that's true. So we can just
            # find the arrow that ends at (x,y), follow it backwards, and
            # repeat until we arrive at (0,0).
            #
            # This wouldn't benefit much from using a hashmap, because the
            # arrows are added in order, so it's fast enough to find them
            # by going through the list backwards.
            print("Diff in reverse order:")
            while x != 0 or y != 0:
                while arrows[-1][:2] != (x, y):
                    arrows.pop()
                letter, arrow_id = arrows.pop()[2:]
                canvas.itemconfig(arrow_id, fill="cyan")
                match letter:
                    case '+':
                        y -= 1
                        print(f"+{b[y]}")
                        draw_text(x+0.25, y+0.5, f"+{b[y]}")
                    case '-':
                        x -= 1
                        print(f"-{a[x]}")
                        draw_text(x+0.5, y+0.2, f"-{a[x]}")
                    case ' ':
                        x -= 1
                        y -= 1
                        assert a[x] == b[y]
                        print(f" {a[x]}")
                        draw_text(x+0.5, y+0.2, f"{a[x]}")
                    case _:
                        raise ValueError("oh no")
                pause()
            tkinter.mainloop()
            exit()
