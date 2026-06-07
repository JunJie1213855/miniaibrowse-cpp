function add(a, b)
    if b == 0 then
        return -1;
    end
    return a / b;
end

print(add(10, 0))

print(add(10, 1))