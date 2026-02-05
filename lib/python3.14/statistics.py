# statistics: mean, median, stdev (pure Python over iterables).

def mean(data):
    """Arithmetic mean of data."""
    data = list(data)
    if not data:
        raise ValueError("mean requires at least one data point")
    return sum(data) / len(data)

def median(data):
    """Median (middle value) of data."""
    data = sorted(list(data))
    n = len(data)
    if not n:
        raise ValueError("median requires at least one data point")
    if n % 2 == 1:
        return data[n // 2]
    return (data[n // 2 - 1] + data[n // 2]) / 2

def stdev(data, xbar=None):
    """Sample standard deviation. If xbar is None, use mean(data)."""
    data = list(data)
    n = len(data)
    if n < 2:
        raise ValueError("variance requires at least two data points")
    if xbar is None:
        xbar = mean(data)
    variance = sum((x - xbar) ** 2 for x in data) / (n - 1)
    return variance ** 0.5

def pvariance(data, mu=None):
    """Population variance. If mu is None, use mean(data)."""
    data = list(data)
    n = len(data)
    if n < 1:
        raise ValueError("variance requires at least one data point")
    if mu is None:
        mu = mean(data)
    return sum((x - mu) ** 2 for x in data) / n

def variance(data, xbar=None):
    """Sample variance. If xbar is None, use mean(data)."""
    data = list(data)
    n = len(data)
    if n < 2:
        raise ValueError("variance requires at least two data points")
    if xbar is None:
        xbar = mean(data)
    return sum((x - xbar) ** 2 for x in data) / (n - 1)

StatisticsError = ValueError
