import random


class MySet:
    def __init__(self):
        self.table = []
        self.count = 0
        self.capacity = 0

    def add_without_growing(self, value):
        pos = value % self.capacity
        while self.table[pos] is not None:
            pos += 1
            pos %= self.capacity
        self.table[pos] = value

    def add(self, value):
        # Keep the hash table at most 50% full
        if 2*(self.count + 1) > self.capacity:
            old_capacity = self.capacity
            if self.capacity == 0:
                self.capacity = 4
            else:
                self.capacity *= 2

            # Allocate a bigger hash table
            old_table = self.table
            self.table = [None] * self.capacity

            # Insert all the old values to their new places
            for old_value in old_table:
                if old_value is not None:
                    self.add_without_growing(old_value)

        self.add_without_growing(value)
        self.count += 1

    def has(self, value):
        if self.capacity == 0:
            return False

        pos = value % len(self.table)
        while self.table[pos] is not None:
            if self.table[pos] == value:
                return True
            pos = (pos+1) % self.capacity

        return False

    def fix_hole(self, hole):
        assert self.table[hole] is None

        # Look at consecutive elements after the hole. Try to move them to
        # where the hole is.
        i = (hole + 1) % self.capacity
        while self.table[i] is not None:
            ideal = self.table[i] % self.capacity

            # These rely on how '%' handles negative numbers in Python and Jou.
            # For example, this code wouldn't work as is in C.
            how_far_it_would_be = (hole - ideal) % self.capacity
            how_far_it_is_now = (i - ideal) % self.capacity

            if how_far_it_would_be < how_far_it_is_now:
                # The element is happier in the hole we're trying to fill than
                # in its current place. Let's move it into the hole.
                self.table[hole] = self.table[i]
                self.table[i] = None

                # We may have created a new hole to where the item was. That
                # needs to be fixed by calling this function again.
                return i

            i = (i + 1) % self.capacity

        # The hole doesn't matter because the ideal place of every element
        # immediately after the hole is somewhere after the hole.
        return -1

    def remove(self, value):
        assert self.capacity > 0

        # Find the element to remove
        i = value % self.capacity
        assert self.table[i] is not None
        while self.table[i] != value:
            i = (i + 1) % self.capacity
            assert self.table[i] is not None

        # This creates a hole in the hash table that may
        # prevent finding other elements that have been placed after the hole.
        self.table[i] = None
        self.count -= 1

        # Fix the hole. Every time it's fixed we may get a new hole. Fix until
        # we have no more holes.
        while i != -1:
            i = self.fix_hole(i)


random.seed(123)

while True:
    python_set = set()
    my_set = MySet()

    for iteration in range(10000):
        assert my_set.count == len(python_set)
        assert my_set.capacity == len(my_set.table)

        n = random.randint(1, 100)
        #print("Looking for", n)
        #print(my_set.table)
        assert my_set.has(n) == (n in python_set), (my_set.table, n)
        if n in python_set:
            #print("Removing", n)
            python_set.remove(n)
            my_set.remove(n)
        else:
            #print("Adding", n)
            python_set.add(n)
            my_set.add(n)
        #print("  -->", my_set.table)

    print(".", end="", flush=True)
