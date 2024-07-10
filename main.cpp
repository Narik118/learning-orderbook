#include <iostream>
#include "Side.h"
#include "OrderType.h"
#include "Constants.h"
#include <list>
#include <map>
#include <numeric>


struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos {
public:
    OrderBookLevelInfos(LevelInfos& bids, LevelInfos& asks){
        bids_ = bids;
        asks_ = asks;
    };

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order {
public: 
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity){
        orderType_ = orderType;
        orderId_ = orderId;
        side_ = side;
        price_ = price;
        quantity_ = quantity;  
        initial_quantity_ = quantity;
        remaining_quantity = quantity;
    };

    OrderId GetOrderId() const {return orderId_ ;}
    OrderType GetOrderType() const {return orderType_ ;}
    Price GetPrice() const {return price_ ;}
    Side GetSide() const {return side_ ;}
    Quantity GetQuantity() const {return quantity_ ;}
    Quantity GetInitialQuantity() const {return initial_quantity_ ;}
    Quantity GetRemainingQuantity() const {return remaining_quantity ;} 
    bool IsFilled() const {return GetRemainingQuantity()==0 ;}

    void Fill(Quantity quantity) 
    {
        if(quantity > GetRemainingQuantity()){
            throw "invalid quantity";
        }

        remaining_quantity -= quantity;
        std::cout << remaining_quantity << std::endl;
    };

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
    Quantity initial_quantity_;
    Quantity remaining_quantity;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
    public:
    OrderModify(OrderId orderId ,Side side,Price price,Quantity quantity)
    : orderId_{orderId}
    , price_{price}
    ,side_{side}
    ,quantity_{quantity}
    {}

    OrderId GetOrderId() const {return orderId_;}
    Price GetPrice() const {return price_;}
    Side GetSide() const {return side_;}
    Quantity GetQuantity() const {return quantity_;}

    OrderPointer ToOrderPointer(OrderType type)const
    {
        return std::make_shared<Order>(type,GetOrderId(),GetSide(),GetPrice(),GetQuantity());
    }

    private:
        OrderId orderId_;
        Price price_;
        Side side_;
        Quantity quantity_;

};

struct TradeInfo
{
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade{
    public:
        Trade(const TradeInfo& bidTrade , const TradeInfo& askTrade)
            : bidTrade_ { bidTrade }
            , askTrade_ { askTrade }
            {}
        
        const TradeInfo& GetBidTrade() const { return bidTrade_; }
        const TradeInfo& GetAskTrade() const { return askTrade_; }

    private:
        TradeInfo bidTrade_ ;
        TradeInfo askTrade_ ;
};

using Trades = std::vector<Trade>; 

class Orderbook
{
    private:

        struct OrderEntry
        {
            OrderPointer order_{ nullptr };
            OrderPointers::iterator location_;
        };

        std::map<Price, OrderPointers , std::greater<Price>> bids_;
        std::map<Price, OrderPointers , std::less<Price>> asks_;
        std::unordered_map<OrderId,OrderEntry> orders_;
        
        bool CanMatch(Side side ,Price price) const 
        {
            if(side==Side::Buy)
            {
                if(asks_.empty())
                    return false;
                
               const auto& [bestAsk, _]= *asks_.begin();
               return price >= bestAsk; 
            }
            else
            {
                if(bids_.empty())
                    return false;

                const auto& [bestBid,_]=*bids_.begin();
                return price <=bestBid;
            }
        }

        Trades MatchOrders()
        {
            Trades trades;
            trades.reserve(orders_.size());

            while(true)
            {
                if(bids_.empty() ||  asks_.empty())
                    break;
                
                auto& [bidPrice,bids]= *bids_.begin();
                auto& [askPrice,asks]= *asks_.begin();

                if(bidPrice < askPrice)
                    break;

                while(bids.size() && asks.size())
                {
                    auto& bid = bids.front();
                    auto& ask =asks.front();

                    Quantity quantity = std::min(bid->GetRemainingQuantity(),ask->GetInitialQuantity());

                    bid->Fill(quantity);
                    ask->Fill(quantity);

                    if(bid->IsFilled())
                    {
                        bids.pop_front();
                        orders_.erase(bid->GetOrderId());
                    }

                    if(ask->IsFilled())
                    {
                        asks.pop_front();
                        orders_.erase(ask->GetOrderId());
                    }

                    if(bids.empty())
                    {
                        bids_.erase(bidPrice);
                    }

                    if(asks.empty())
                        asks_.erase(askPrice);

                    trades.push_back(Trade{
                        TradeInfo{bid->GetOrderId(),bid->GetPrice(), quantity},
                        TradeInfo{ask->GetOrderId(),ask->GetPrice(), quantity}
                    
                    });
                }
            }

            if(!bids_.empty())
            {
                auto& [_,bids]= *bids_.begin();
                auto& order = bids.front();
                if(order->GetOrderType()== OrderType::FillAndKill)
                    CancelOrder(order->GetOrderId());
            }

            if(!asks_.empty())
            {
                auto& [_,asks]= *asks_.begin();
                auto& order = asks.front();
                if(order->GetOrderType()== OrderType::FillAndKill)
                    CancelOrder(order->GetOrderId());
            }
            return trades;
        }

    public:

        Trades AddOrder(OrderPointer order)
        {
            if(orders_.find(order->GetOrderId()) == orders_.end())
                return { };

            if(order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(),order->GetPrice()))
                return { };

            OrderPointers::iterator iterator;

            if(order->GetSide()==Side::Buy)
            {
                auto& orders=bids_[order->GetPrice()];
                orders.push_back(order);
                iterator= std::next(orders.begin(),orders.size()-1);
            }
            else
            {
                auto& orders = asks_[order->GetPrice()];
                orders.push_back(order);
                iterator=std::next(orders.begin(),orders.size()-1);
            }

            orders_.insert({order->GetOrderId(),OrderEntry{order,iterator}});
            return MatchOrders();
        }

        void CancelOrder(OrderId orderId)
        {
if (orders_.find(orderId) == orders_.end()) {
    return; 
}

            const auto& [order ,iterator]=orders_.at(orderId);
            orders_.erase(orderId);

            if(order->GetSide()==Side::Sell)
            {
                auto price =order->GetPrice();
                auto& orders=asks_.at(price);
                orders.erase(iterator);
                if(orders.empty())
                {
                    asks_.erase(price);
                }
            }
            else
            {
                auto price = order->GetPrice();
                auto&  orders=bids_.at(price);
                orders.erase(iterator);
                if(orders.empty())
                    bids_.erase(price);
            }
        }

Trades MatchOrder(OrderModify order) {
    auto it = orders_.find(order.GetOrderId());
    if (it == orders_.end()) {
        return { }; 
    }

    const auto& [existingOrder, _] = it->second; 
    CancelOrder(order.GetOrderId()); 
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType())); 
}

        std::size_t Size() const { return orders_.size();}

OrderBookLevelInfos GetOrderInfos() const {
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size());
    askInfos.reserve(orders_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers& orders) {
        Quantity totalQuantity = 0;
        for (const auto& order : orders) {
            totalQuantity += order->GetRemainingQuantity();
        }
        return LevelInfo{ price, totalQuantity };
    };

    for (const auto& [price, orders] : bids_)
        bidInfos.push_back(CreateLevelInfos(price, orders));

    for (const auto& [price, orders] : asks_)
        askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderBookLevelInfos{ bidInfos, askInfos };
}

};



int main() {

    Orderbook Orderbook;

    OrderId orderId = 238623;
    Quantity quantity = 10;
    Order order1(OrderType::FillAndKill, orderId, Side::Buy, 1000, 10);

    order1.Fill(quantity);
    std::cout << order1.GetOrderId() << std::endl;

    return 0;
}