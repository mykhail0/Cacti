"""Compute a factorial of a given number."""

from math import prod


def factorial(n: int) -> int:
    """Compute a factorial of a given number."""
    return prod(range(1, n + 1))


def main() -> None:
    """Compute a factorial of a given number from stdin and print to stdout."""
    n = int(input())
    print(factorial(n))


if __name__ == "__main__":
    main()
